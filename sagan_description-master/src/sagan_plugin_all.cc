#ifndef _SAGAN_PLUGIN_HH_
#define _SAGAN_PLUGIN_HH_

#include <gazebo/gazebo.hh>
#include <gazebo/physics/physics.hh>
#include <gazebo/common/common.hh>
#include <ignition/math/Vector3.hh>
#include <gazebo/transport/transport.hh>
#include <gazebo/msgs/msgs.hh>
#include "ros/ros.h"
#include "ros/callback_queue.h"
#include "ros/subscribe_options.h"
#include <thread>
#include <iostream>
#include <time.h>
#include "geometry_msgs/Twist.h"
#include "sensor_msgs/JointState.h"

namespace gazebo
{
  /// \brief A plugin to control a Velodyne sensor.
  class SaganPlugin : public ModelPlugin
  {
    /// \brief Constructor
    public: SaganPlugin() {}

    /// \brief The load function is called by Gazebo when the plugin is
    /// inserted into simulation
    /// \param[in] _model A pointer to the model that this plugin is
    /// attached to.
    /// \param[in] _sdf A pointer to the plugin's SDF element.
    public: virtual void Load(physics::ModelPtr model, sdf::ElementPtr sdf)
    {
      // Just output a message for now
      std::cerr << "\nThe Sagan plugin is attach to model[" << model->GetName() << "]\n";
    
      this->model = model;

      this->pid_motor = common::PID(0.1, 0, 0);

      //Get Joint Pointer
      this->BLW_joint = model->GetJoints()[1];
      this->FLW_joint = model->GetJoints()[2];
      this->BRW_joint = model->GetJoints()[4];
      this->FRW_joint = model->GetJoints()[5];

      //Apply P controller to the joints
      this->model->GetJointController()->SetVelocityPID(this->BLW_joint->GetScopedName(), this->pid_motor);
      this->model->GetJointController()->SetVelocityPID(this->FLW_joint->GetScopedName(), this->pid_motor);
      this->model->GetJointController()->SetVelocityPID(this->BRW_joint->GetScopedName(), this->pid_motor);
      this->model->GetJointController()->SetVelocityPID(this->FRW_joint->GetScopedName(), this->pid_motor);


      // Create our ROS node
      this->rosNode.reset(new ros::NodeHandle("sagan_gazebo"));

      // Create a named topic, and subscribe to it.
      ros::SubscribeOptions so = ros::SubscribeOptions::create<geometry_msgs::Twist>("/sagan_gazebo/vel_cmd", 1, 
      boost::bind(&SaganPlugin::OnNewCmd, this, _1), ros::VoidPtr(), &this->rosQueue);

      this->rosSub_vel = this->rosNode->subscribe(so);

      // Spin up the queue helper thread.
      this->rosQueueThread = std::thread(std::bind(&SaganPlugin::QueueThread, this));

      // Configure Timer and callback function 
	this->pubTimer = this->rosNode->createTimer(ros::Duration(0.01), &SaganPlugin::StateCallback, this);

	// Create a topic to publish iris state
	this->state_pub = this->rosNode->advertise<sensor_msgs::JointState>("/sagan_gazebo/state", 100);

    }

    // ROS helper function that processes messages
    private: void QueueThread()
    {
      static const double timeout = 0.01;
      while (this->rosNode->ok())
      {
        this->rosQueue.callAvailable(ros::WallDuration(timeout));
      }
    }

    public: void OnNewCmd(const geometry_msgs::TwistConstPtr &msg)
    {
      //Update linear and angular cmd
      this->vel_linear_cmd = msg->linear.z;
      this->vel_angular_cmd = msg->angular.z;

	std::cout << "\nnew comand\n";
    

      //Convert to wheel cmd
      this->DiffDriverVel();

      //Update Motor cmd
      this->MotorCmd();
    }

    public: void MotorCmd()
    {
    // Set the joint's target velocity.
      this->model->GetJointController()->SetVelocityTarget(this->BLW_joint->GetScopedName(), BLW_cmd);

      this->model->GetJointController()->SetVelocityTarget(this->FLW_joint->GetScopedName(), FLW_cmd);

      this->model->GetJointController()->SetVelocityTarget(this->BRW_joint->GetScopedName(), BRW_cmd);

      this->model->GetJointController()->SetVelocityTarget(this->FRW_joint->GetScopedName(), FRW_cmd);
    }

    public: void DiffDriverVel()
    {

      this->FLW_cmd = (this->vel_linear_cmd - this->vel_angular_cmd*this->wheel_sep/2)/this->wheel_radius;
      this->FRW_cmd = (this->vel_linear_cmd + this->vel_angular_cmd*this->wheel_sep/2)/this->wheel_radius;
  
      this->BLW_cmd = this->FLW_cmd;
      this->BRW_cmd = this->FRW_cmd;
    }

    //Function to update states and pub information
    public: void StateCallback(const ros::TimerEvent& event){
	
	    //Get the current position and velocity	
	    math::Pose sagan_pose = this->model->GetWorldPose(); 
	    math::Vector3 sagan_linear_vel, sagan_angular_vel;
	    sagan_linear_vel = this->model->GetWorldLinearVel();
	    sagan_angular_vel = this->model->GetWorldAngularVel();

		//Update state variables
		this->sagan_state[0] = sagan_pose.pos[0];
		this->sagan_state[1] = sagan_pose.pos[1];
		this->sagan_state[2] = sagan_pose.pos[2];
		this->sagan_state[3] = sagan_pose.pos[3];
		this->sagan_state[4] = sagan_pose.pos[4];
		this->sagan_state[5] = sagan_pose.pos[5];
		 
		this->sagan_vel[0] = sagan_linear_vel[0];
		this->sagan_vel[1] = sagan_linear_vel[1];
		this->sagan_vel[2] = sagan_linear_vel[2];
		this->sagan_vel[3] = sagan_angular_vel[0];
		this->sagan_vel[4] = sagan_angular_vel[1];
		this->sagan_vel[5] = sagan_angular_vel[2];

		// Create a variable to publish the state
		sensor_msgs::JointState pub_sagan_state;

		for(int it = 0; it < 6; it++){
		   pub_sagan_state.position[it] = this->sagan_state[it];
		   pub_sagan_state.velocity[it] = this->sagan_vel[it];
		}

		//Publish data
		this->state_pub.publish(pub_sagan_state);
	}


 	//Sagan parameters
 	private: double wheel_radius = 0.2;

  	private: double wheel_sep = 0.3;

  	//Sagan pointers
	private: physics::ModelPtr model;

	private: physics::JointPtr BLW_joint;

	private: physics::JointPtr FLW_joint;

	private: physics::JointPtr BRW_joint;

	private: physics::JointPtr FRW_joint;

	private: common::PID pid_motor;

	//Command variables
	private: double BLW_cmd = 0;

	private: double FLW_cmd = 0;

	private: double BRW_cmd = 0;

	private: double FRW_cmd = 0;

	private: double vel_linear_cmd = 0;

	private: double vel_angular_cmd = 0;

	//Sagan data
	private: double sagan_state[6] = {0,0,0,0,0,0};

	private: double sagan_vel[6] = {0,0,0,0,0,0};

 	// A node use for ROS transport
	private: std::unique_ptr<ros::NodeHandle> rosNode;
  
	// A ROS subscriber
	private: ros::Subscriber rosSub_vel;

 	// A ROS callbackqueue that helps process messages
	private: ros::CallbackQueue rosQueue;

 	// A thread the keeps running the rosQueue
 	private: std::thread rosQueueThread;  

	// Publisher Timer
	private: ros::Timer pubTimer;

	//Publisher Sagan State
	private: ros::Publisher state_pub;


  };
  // Tell Gazebo about this plugin, so that Gazebo can call Load on this plugin.
  GZ_REGISTER_MODEL_PLUGIN(SaganPlugin)
}
#endif
