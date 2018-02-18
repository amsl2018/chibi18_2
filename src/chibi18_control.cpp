#include <ros/ros.h>
#include <boost/thread.hpp>
#include <geometry_msgs/Pose.h>
#include <tf/transform_broadcaster.h>
#include "roomba_500driver_meiji/RoombaCtrl.h"
#include <iostream>
#include <math.h>
#include <nav_msgs/Odometry.h>
#include <std_msgs/Float32.h>
#include <std_msgs/String.h>
#include <geometry_msgs/Pose2D.h>
#include <geometry_msgs/Twist.h>

nav_msgs::Odometry roomba_odometry;//グローバル変数
boost::mutex roomba_odometry_mutex;//mutexオブジェクト
geometry_msgs::Pose2D target;//目的地(/world)
boost::mutex target_mutex;//mutexオブジェクトその２

void roomba_odometry_callback(const nav_msgs::OdometryConstPtr& msg)
{
  boost::mutex::scoped_lock(roomba_odometry_mutex);
  roomba_odometry = *msg;
}

void chibi18_target_callback(const geometry_msgs::Pose2DConstPtr& msg)
{
  boost::mutex::scoped_lock(target_mutex);
  target = *msg;
}

int main(int argc, char** argv){
  ros::init(argc, argv, "chibi18_control");
  ros::NodeHandle nh;
  
  ros::Publisher controller_pub = nh.advertise<roomba_500driver_meiji::RoombaCtrl>("/roomba/control", 100);

  ros::Subscriber odometry_sub = nh.subscribe("/roomba/odometry", 100, roomba_odometry_callback);

  ros::Subscriber target_sub = nh.subscribe("/chibi18/target", 100, chibi18_target_callback);

  ros::Rate loop_rate(10);

  while(ros::ok()){
    nav_msgs::Odometry roomba_position;//roomba_odometryのローカル変数 
    roomba_500driver_meiji::RoombaCtrl roomba_velocity;
    //mutexロック　去年のと書き方が違う
    {
      boost::mutex::scoped_lock(roomba_odometry_mutex);
      roomba_position = roomba_odometry;
    }
    geometry_msgs::Pose2D _target;//targetのローカル変数
    {
      boost::mutex::scoped_lock(target_mutex);
      _target = target;
    } 
    float vx = 0.5 * sqrt(pow(_target.x - roomba_position.pose.pose.position.x, 2) + pow(_target.y - roomba_position.pose.pose.position.y, 2));//0.5は適当  
    if(vx < -1.0){
      vx = -1.0;
    }else if(vx > 1.0){
      vx = 1.0;
    }
    roomba_velocity.cntl.linear.x = vx; 

    float omega_z = 0.5 * atan2((_target.x - roomba_position.pose.pose.position.x), (_target.y - roomba_position.pose.pose.position.y));//0.5は適当
    if(omega_z < -1.0){
      omega_z = -1.0;
    }else if(omega_z > 1.0){
      omega_z = 1.0;
    }
    roomba_velocity.cntl.angular.z = omega_z;

    controller_pub.publish(roomba_velocity);

    ros::spinOnce();
    loop_rate.sleep();
  }
  return 0;
}
