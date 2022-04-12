#include "active_3d_planning_core/module/trajectory_evaluator/yaw_planning_evaluator.h"

#include <limits>
#include <string>
#include <vector>

#include "active_3d_planning_core/module/module_factory.h"
#include "active_3d_planning_core/planner/planner_I.h"

namespace active_3d_planning {
namespace trajectory_evaluator {

// YawPlanningEvaluator
YawPlanningEvaluator::YawPlanningEvaluator(PlannerI& planner)
    : TrajectoryEvaluator(planner) {}

void YawPlanningEvaluator::setupFromParamMap(Module::ParamMap* param_map) {
  setParam<int>(param_map, "n_directions", &p_n_directions_, 4);
  setParam<bool>(param_map, "select_by_value", &p_select_by_value_, false);
  setParam<double>(param_map, "update_range", &p_update_range_,
                   -1.0);  // default is no updates
  setParam<double>(param_map, "update_gain", &p_update_gain_, 0.0);

  // Register link for yaw planning udpaters
  planner_.getFactory().registerLinkableModule("YawPlanningEvaluator", this);

  // Create following evaluator
  std::string args;  // default args extends the parent namespace
  std::string param_ns = (*param_map)["param_namespace"];
  setParam<std::string>(param_map, "following_evaluator_args", &args,
                        param_ns + "/following_evaluator");
  following_evaluator_ =
      planner_.getFactory().createModule<TrajectoryEvaluator>(args, planner_,
                                                              verbose_modules_);

  // setup parent
  TrajectoryEvaluator::setupFromParamMap(param_map);
}

bool YawPlanningEvaluator::checkParamsValid(std::string* error_message) {
  if (p_n_directions_ < 1) {
    *error_message = "n_directions expected > 0";
    return false;
  }
  return TrajectoryEvaluator::checkParamsValid(error_message);
}

bool YawPlanningEvaluator::computeGain(TrajectorySegment* traj_in) {
  // Init
  double start_yaw = traj_in->trajectory.front().getYaw();
  double original_yaw = traj_in->trajectory.back().getYaw();
  YawPlanningInfo* info =
      new YawPlanningInfo();  // already create the info struct and directly
  // populate it //where is this getting deleted??
  info->active_orientation = 0;

  // Get value for initial position
  info->orientations.push_back(traj_in->shallowCopy());
  setTrajectoryYaw(&(info->orientations.back()), start_yaw, original_yaw);
  following_evaluator_->computeGain(&(info->orientations.back()));
  double current_value = info->orientations.back().gain;
  if (p_select_by_value_) {
    following_evaluator_->computeCost(&(info->orientations.back()));
    following_evaluator_->computeValue(&(info->orientations.back()));
    current_value = info->orientations.back().value;
  }
  double best_value = current_value;

  // Sample all directions
  for (int i = 1; i < p_n_directions_; ++i) {
    info->orientations.push_back(traj_in->shallowCopy());
    setTrajectoryYaw(&(info->orientations.back()), start_yaw,
                     sampleYaw(original_yaw, i));
    following_evaluator_->computeGain(&(info->orientations.back()));
    if (p_select_by_value_) {
      following_evaluator_->computeCost(&(info->orientations.back()));
      following_evaluator_->computeValue(&(info->orientations.back()));
      current_value = info->orientations.back().value;
    } else {
      current_value = info->orientations.back().gain;
    }
    if (current_value > best_value) {
      best_value = current_value;
      info->active_orientation = i;
    }
  }

  // Apply best segment to the original one, store rest in info
  traj_in->trajectory = info->orientations[info->active_orientation].trajectory;
  traj_in->gain = info->orientations[info->active_orientation].gain;
  if (p_select_by_value_) {
    traj_in->cost = info->orientations[info->active_orientation].cost;
    traj_in->value = info->orientations[info->active_orientation].value;
  }
  traj_in->info.reset(info);
  return true;
}

bool YawPlanningEvaluator::computeCost(TrajectorySegment* traj_in) {
  return following_evaluator_->computeCost(traj_in);
}

bool YawPlanningEvaluator::computeValue(TrajectorySegment* traj_in) {
  return following_evaluator_->computeValue(traj_in);
}

int YawPlanningEvaluator::selectNextBest(TrajectorySegment* traj_in) {
  return following_evaluator_->selectNextBest(traj_in);
}

bool YawPlanningEvaluator::updateSegment(TrajectorySegment* segment) {
  if (segment->parent && segment->info) {
    double dist =
        (planner_.getCurrentPosition() - segment->trajectory.back().position_W)
            .norm();
    if (p_update_range_ == 0.0 || p_update_range_ > dist) {
      YawPlanningInfo* info =
          reinterpret_cast<YawPlanningInfo*>(segment->info.get());
      double current_value = std::numeric_limits<double>::lowest();
      double best_value = std::numeric_limits<double>::lowest();
      for (int i = 0; i < info->orientations.size(); ++i) {
        if (info->orientations[i].gain > p_update_gain_) {
          // all conditions met: update gain and wiring of segment.
          double start_yaw = segment->trajectory.front().getYaw();
          double original_yaw =
              info->orientations[i].trajectory.back().getYaw();
          // info->orientations[i].trajectory = segment->trajectory;
          // setTrajectoryYaw(&(info->orientations[i]), start_yaw, original_yaw);
          following_evaluator_->computeGain(&(info->orientations[i]));
          if (p_select_by_value_) {
            following_evaluator_->computeCost(&(info->orientations[i]));
            following_evaluator_->computeValue(&(info->orientations[i]));
            current_value = info->orientations[i].value;
          } else {
            current_value = info->orientations[i].gain;
          }
          if (current_value > best_value) {
            best_value = current_value;
            info->active_orientation = i;
          }
        }
      }
      // Update trajectory
      // segment->trajectory =
      //     info->orientations[info->active_orientation].trajectory;
      segment->gain = info->orientations[info->active_orientation].gain;
      setTrajectoryYaw(segment, segment->trajectory.front().getYaw(), info->orientations[info->active_orientation].trajectory.back().getYaw());
      if (p_select_by_value_) {
        segment->cost = info->orientations[info->active_orientation].cost;
        segment->value = info->orientations[info->active_orientation].value;
      }
    }
  }
  return following_evaluator_->updateSegment(segment);
}

void YawPlanningEvaluator::visualizeTrajectoryValue(
    VisualizationMarkers* markers, const TrajectorySegment& trajectory) {
  if (!trajectory.info) {
    return;
  }
  YawPlanningInfo* info =
      reinterpret_cast<YawPlanningInfo*>(trajectory.info.get());
  // Let the followup evaluator draw the visualization of the selected
  // orientation
  following_evaluator_->visualizeTrajectoryValue(
      markers, info->orientations[info->active_orientation]);
}

}  // namespace trajectory_evaluator
}  // namespace active_3d_planning
