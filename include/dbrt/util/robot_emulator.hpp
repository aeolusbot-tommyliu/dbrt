/*
 * This is part of the Bayesian Object Tracking (bot),
 * (https://github.com/bayesian-object-tracking)
 *
 * Copyright (c) 2015 Max Planck Society,
 * 				 Autonomous Motion Department,
 * 			     Institute for Intelligent Systems
 *
 * This Source Code Form is subject to the terms of the GNU General Public
 * License License (GNU GPL). A copy of the license can be found in the LICENSE
 * file distributed with this source code.
 */

/**
 * \file virtual_robot.hpp
 * \date January 2016
 * \author Jan Issac (jan.issac@gmail.com)
 */

#pragma once

#include <memory>
#include <thread>
#include <functional>

#include <sensor_msgs/Image.h>

#include <dbot/util/camera_data.hpp>
#include <dbot/util/object_model.hpp>
#include <dbot/util/rigid_body_renderer.hpp>
#include <dbot_ros/utils/ros_interface.hpp>

#include <dbrt/robot_publisher.h>
#include <dbrt/util/kinematics_from_urdf.hpp>

namespace dbrt
{
class RobotAnimator
{
public:
    virtual void animate(const Eigen::VectorXd& current,
                         double dt,
                         double dilation,
                         Eigen::VectorXd& next) = 0;
};

template <typename State>
class RobotEmulator
{
public:
    /**
     * \brief Creates a VirtualRobot
     */
    RobotEmulator(const std::shared_ptr<dbot::ObjectModel>& object_model,
                  const std::shared_ptr<KinematicsFromURDF>& urdf_kinematics,
                  const std::shared_ptr<dbot::RigidBodyRenderer>& renderer,
                  const std::shared_ptr<dbot::CameraData>& camera_data,
                  const std::shared_ptr<RobotAnimator>& robot_animator,
                  double joint_sensors_rate,
                  double visual_sensor_rate,
                  double dilation,
                  const State& initial_state)
        : state_(initial_state),
          object_model_(object_model),
          urdf_kinematics_(urdf_kinematics),
          renderer_(renderer),
          camera_data_(camera_data),
          robot_animator_(robot_animator),
          joint_sensors_rate_(joint_sensors_rate),
          visual_sensor_rate_(visual_sensor_rate),
          dilation_(dilation)
    {
        robot_tracker_publisher_simulated_ =
            std::make_shared<RobotTrackerPublisher<State>>(
                urdf_kinematics_, renderer_, "/robot_emulator", "");

        render_and_publish();
    }

    void run()
    {
        running_ = true;
        joint_sensor_thread_ =
            std::thread(&RobotEmulator::run_joint_sensors, this);
        visual_sensor_thread_ =
            std::thread(&RobotEmulator::run_visual_sensors, this);
    }

    void shutdown()
    {
        running_ = false;
        joint_sensor_thread_.join();
        visual_sensor_thread_.join();
    }

    void run_joint_sensors()
    {
        const double rate = joint_sensors_rate_;
        ros::Rate joint_rate(rate);
        while (running_)
        {
            joint_rate.sleep();
            std::lock_guard<std::mutex> state_lock(state_mutex_);
            robot_animator_->animate(state_, 1. / rate, dilation_, state_);
            robot_tracker_publisher_simulated_->publish_joint_state(state_);
        }
    }

    void run_visual_sensors()
    {
        ros::Rate image_rate(visual_sensor_rate_);
        while (running_)
        {
            image_rate.sleep();
            render_and_publish();
        }
    }

    State state() const
    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        return state_;
    }

    Eigen::VectorXd joint_observation()
    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        return state_;
    }

    const sensor_msgs::Image& observation()
    {
        std::lock_guard<std::mutex> image_lock(image_mutex_);
        return obsrv_image_;
    }

    const Eigen::VectorXd& observation_vector()
    {
        std::lock_guard<std::mutex> image_lock(image_mutex_);
        return obsrv_vector_;
    }

private:
    void render_and_publish()
    {
        State current_state = state();

        std::lock_guard<std::mutex> image_lock(image_mutex_);

        // render observation image
        renderer_->Render(current_state,
                          obsrv_vector_,
                          std::numeric_limits<double>::quiet_NaN());

        // convert image vector to ros image message
        robot_tracker_publisher_simulated_->convert_to_depth_image_msg(
            camera_data_, obsrv_vector_, obsrv_image_);

        // publish observation image and point cloud
        robot_tracker_publisher_simulated_->publish(
            current_state, obsrv_image_, camera_data_);

        robot_tracker_publisher_simulated_->publish_camera_info(camera_data_);
    }

private:
    Eigen::VectorXd state_;
    Eigen::VectorXd obsrv_vector_;
    sensor_msgs::Image obsrv_image_;
    std::shared_ptr<dbot::ObjectModel> object_model_;
    std::shared_ptr<KinematicsFromURDF> urdf_kinematics_;
    std::shared_ptr<dbot::RigidBodyRenderer> renderer_;
    std::shared_ptr<dbot::CameraData> camera_data_;
    std::shared_ptr<RobotAnimator> robot_animator_;
    std::shared_ptr<RobotTrackerPublisher<State>>
        robot_tracker_publisher_simulated_;
    double joint_sensors_rate_;
    double visual_sensor_rate_;
    double dilation_;

    bool running_;
    mutable std::mutex state_mutex_;
    mutable std::mutex image_mutex_;
    std::thread joint_sensor_thread_;
    std::thread visual_sensor_thread_;
};
}