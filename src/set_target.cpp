#include <ros/ros.h>
#include <boost/thread.hpp>
#include <geometry_msgs/PoseStamped.h>
#include <iostream>
#include <tf/tf.h>

int main(int argc, char** argv)
{
  ros::init(argc, argv, "set_target");
  ros::NodeHandle nh;
  ros::NodeHandle local_nh("~");

  geometry_msgs::PoseStamped target;
  target.pose.orientation.w = 1.0;
  local_nh.getParam("X", target.pose.position.x);
  local_nh.getParam("Y", target.pose.position.y);
  float yaw;
  local_nh.getParam("YAW", yaw);
  target.pose.orientation = tf::createQuaternionMsgFromYaw(yaw);


  target.header.frame_id = "odom";

  std::cout << "x=" << target.pose.position.x << " y=" << target.pose.position.y << std::endl;

  ros::Publisher target_pub = nh.advertise<geometry_msgs::PoseStamped>("/chibi18/target", 100);

  ros::Rate loop_rate(10);

  while(ros::ok()){
    target_pub.publish(target);
    
    ros::spinOnce();
    loop_rate.sleep();
  }
}
