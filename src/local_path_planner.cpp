#include <ros/ros.h>
#include <geometry_msgs/Twist.h>
#include <sensor_msgs/LaserScan.h>
#include <nav_msgs/Odometry.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>
#include <math.h>
#include <std_msgs/Bool.h>
#include <geometry_msgs/Pose2D.h>
#include <nav_msgs/Path.h>
#include <geometry_msgs/TwistStamped.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/PoseArray.h>

//物理量に修正すること
double MAX_VELOCITY;
double MIN_VELOCITY;
double MAX_ANGULAR_VELOCITY;
double MAX_ACCELERATION;
double MAX_ANGULAR_ACCELERATION;
double VELOCITY_RESOLUTION;
double ANGULAR_VELOCITY_RESOLUTION;
const double INTERVAL = 0.100;
double SIMULATE_TIME;
const double LASER_RESOLUTION = 0.00436332312;//[rad]
double RATIO;
double LIMIT_DISTANCE;
double ROBOT_RADIUS;
double GOAL_XY_TOLERANCE;
double GOAL_YAW_TOLERANCE;

//評価関数の係数
double ALPHA = 0;//heading
double BETA = 0;//distance
double GAMMA = 0;//velocity

//DynamicWindowの辺
double window_left = -MAX_ANGULAR_VELOCITY;
double window_up = MAX_VELOCITY;
double window_right = MAX_ANGULAR_VELOCITY;
double window_down = 0;

//subscribe用
nav_msgs::Odometry previous_odometry;
nav_msgs::Odometry current_odometry;
geometry_msgs::Twist velocity_odometry;
geometry_msgs::PoseStamped goal;
geometry_msgs::PoseStamped _goal;
sensor_msgs::LaserScan laser_data;
sensor_msgs::LaserScan _laser_data;//計算用
bool odometry_subscribed = false;
bool target_subscribed = false;
bool move_allowed = false;

geometry_msgs::PoseArray poses;

void evaluate(geometry_msgs::Twist&);
double calcurate_heading(double, double, geometry_msgs::Pose&);
double calcurate_distance(double, double, geometry_msgs::Pose&);
double calcurate_velocity(double);
void calcurate_dynamic_window(void);

double get_larger(double, double);
double get_smaller(double, double);
double get_yaw(geometry_msgs::Quaternion);

void target_callback(const geometry_msgs::PoseStampedConstPtr& msg)
{
  _goal = *msg;
  target_subscribed = true;
}

void laser_callback(const sensor_msgs::LaserScanConstPtr& msg)
{
  laser_data = *msg;
}

void stopper_callback(const std_msgs::BoolConstPtr& msg)
{
  move_allowed = msg->data;
}

int main(int argc, char** argv)
{
    ros::init(argc, argv, "local_path_planner");
    ros::NodeHandle nh;

    ros::NodeHandle local_nh("~");
    local_nh.getParam("ALPHA", ALPHA);
    local_nh.getParam("BETA", BETA);
    local_nh.getParam("GAMMA", GAMMA);
    local_nh.getParam("MAX_VELOCITY", MAX_VELOCITY);
    local_nh.getParam("MIN_VELOCITY", MIN_VELOCITY);
    local_nh.getParam("MAX_ANGULAR_VELOCITY", MAX_ANGULAR_VELOCITY);
    local_nh.getParam("MAX_ACCELERATION", MAX_ACCELERATION);
    local_nh.getParam("MAX_ANGULAR_ACCELERATION", MAX_ANGULAR_ACCELERATION);
    local_nh.getParam("VELOCITY_RESOLUTION", VELOCITY_RESOLUTION);
    local_nh.getParam("ANGULAR_VELOCITY_RESOLUTION", ANGULAR_VELOCITY_RESOLUTION);
    local_nh.getParam("SIMULATE_TIME", SIMULATE_TIME);
    local_nh.getParam("RATIO", RATIO);
    local_nh.getParam("LIMIT_DISTANCE", LIMIT_DISTANCE);
    local_nh.getParam("ROBOT_RADIUS", ROBOT_RADIUS);
    local_nh.getParam("GOAL_YAW_TOLERANCE", GOAL_YAW_TOLERANCE);
    local_nh.getParam("GOAL_XY_TOLERANCE", GOAL_XY_TOLERANCE);

    ros::Subscriber laser_sub = nh.subscribe("/scan", 100, laser_callback);

    ros::Publisher velocity_pub = nh.advertise<geometry_msgs::Twist>("/chibi18/velocity", 100);

    ros::Subscriber target_sub = nh.subscribe("/chibi18/target", 100, target_callback);

    ros::Subscriber stopper_sub = nh.subscribe("/chibi18/stop", 100, stopper_callback);

    tf::TransformListener listener;

    ros::Publisher path_pub = nh.advertise<nav_msgs::Path>("/chibi18/local_path", 100);

    ros::Publisher poses_pub = nh.advertise<geometry_msgs::PoseArray>("/chibi18/local_poses", 100);

    ros::Rate loop_rate(10);

    geometry_msgs::TwistStamped velocity;
    velocity.header.frame_id = "base_link";

    goal.pose.orientation.w = 1;
    _goal.pose.orientation.w = 1;

    poses.header.frame_id = "base_link";

    while(ros::ok()){
      std::cout << "LOOP" << std::endl;
      try{
        previous_odometry = current_odometry;
        tf::StampedTransform transform;
        listener.lookupTransform("odom", "base_link", ros::Time(0), transform);
        current_odometry.pose.pose.position.x = transform.getOrigin().x();
        current_odometry.pose.pose.position.y = transform.getOrigin().y();
        current_odometry.pose.pose.orientation = tf::createQuaternionMsgFromYaw(tf::getYaw(transform.getRotation()));

        velocity_odometry.linear.x = sqrt(pow(current_odometry.pose.pose.position.x-previous_odometry.pose.pose.position.x, 2) + pow(current_odometry.pose.pose.position.y-previous_odometry.pose.pose.position.y, 2))/INTERVAL;
        velocity_odometry.angular.z = (get_yaw(current_odometry.pose.pose.orientation)-get_yaw(previous_odometry.pose.pose.orientation))/INTERVAL;
        if(!std::isnan(velocity_odometry.angular.z)){
          odometry_subscribed = true;
        }
      }catch(tf::TransformException ex){
        std::cout << ex.what() << std::endl;

      }
      if(odometry_subscribed && target_subscribed && !laser_data.ranges.empty()){
        calcurate_dynamic_window();

        listener.transformPose("odom", _goal, goal);

        evaluate(velocity.twist);

        //calculate local path
        double dt = 0.01;
        nav_msgs::Path local_path;
        local_path.header.frame_id = "odom";
        geometry_msgs::PoseStamped pose;
        pose.pose = current_odometry.pose.pose;
        local_path.poses.push_back(pose);
        geometry_msgs::PoseStamped _pose;
        _pose.header.frame_id = "base_link";
        for(double t=0;t<SIMULATE_TIME;t+=dt){
           _pose.pose.position.x += velocity.twist.linear.x * cos(velocity.twist.angular.z * t) * dt;
           _pose.pose.position.y += velocity.twist.linear.x * sin(velocity.twist.angular.z * t) * dt;
           _pose.pose.orientation.w = 1;
           listener.transformPose("odom", _pose, pose);
           local_path.poses.push_back(pose);
        }
        path_pub.publish(local_path);
        poses_pub.publish(poses);

        double distance_to_goal = sqrt(pow(goal.pose.position.x - current_odometry.pose.pose.position.x, 2) + pow(goal.pose.position.y-current_odometry.pose.pose.position.y, 2));
        std::cout << distance_to_goal << "[m]" << std::endl;
        /*
        velocity.twist.linear.x *= RATIO / MAX_VELOCITY;
        velocity.twist.angular.z *= (1-RATIO) / MAX_ANGULAR_VELOCITY;
        */

        if(distance_to_goal < GOAL_XY_TOLERANCE){
          velocity.twist.linear.x = 0;
          double goal_diff = get_yaw(goal.pose.orientation) - get_yaw(current_odometry.pose.pose.orientation);
          if(fabs(goal_diff) < GOAL_YAW_TOLERANCE){
            velocity.twist.angular.z = 0;
          }else{
            velocity.twist.angular.z = goal_diff;
          }
        }

        if(!move_allowed){
          velocity.twist.linear.x = 0.0;
          velocity.twist.angular.z = 0.0;
        }
        std::cout << "order" << std::endl;
        std::cout << velocity.twist.linear.x << "[m/s], " << velocity.twist.angular.z << "[rad/s]" << std::endl;
        std::cout << "(" << current_odometry.pose.pose.position.x << ", " << current_odometry.pose.pose.position.y << ", " << get_yaw(current_odometry.pose.pose.orientation) << ")" << std::endl;

        velocity_pub.publish(velocity.twist);
        std::cout << "goal:" <<  goal.pose.position.x <<" "<< goal.pose.position.y << std::endl;
      }
      std::cout << std::endl;
      ros::spinOnce();
      loop_rate.sleep();
    }
}


void evaluate(geometry_msgs::Twist& velocity)
{
  std::vector<std::vector<double> > e;
  std::vector<std::vector<bool> > allowed;
  std::vector<std::vector<double> > e_heading;
  std::vector<std::vector<double> > e_distance;

  int elements_v = int((window_up - window_down)/VELOCITY_RESOLUTION);
  int elements_o = int((window_right - window_left)/ANGULAR_VELOCITY_RESOLUTION);
  e.resize(elements_v);
  allowed.resize(elements_v);
  e_heading.resize(elements_v);
  e_distance.resize(elements_v);
  for(int i = 0;i<elements_v;i++){
    e[i].resize(elements_o);
    allowed[i].resize(elements_o);
    e_heading[i].resize(elements_o);
    e_distance[i].resize(elements_o);
  }
  _laser_data = laser_data;
  poses.poses.clear();
  double sum_heading = 0;
  double sum_distance = 0;
  for(int v = 0;v < elements_v;v++){
    for(int o = 0;o < elements_o;o++){
      double _velocity = window_down + v * VELOCITY_RESOLUTION;
      double _omega = window_left + o * ANGULAR_VELOCITY_RESOLUTION;
      //std::cout << std::setprecision(4) << "(" << _velocity << "," << _omega << ")";
      geometry_msgs::Pose pose;
      double dt = 0.01;
      double theta = 0;
      for(double t=0;t<SIMULATE_TIME;t+=dt){
        pose.position.x += _velocity * cos(_omega * t) * dt;
        pose.position.y += _velocity * sin(_omega * t) * dt;
        theta += _omega * dt;
      }
      pose.orientation = tf::createQuaternionMsgFromYaw(theta);
      poses.poses.push_back(pose);

      e_heading[v][o] = calcurate_heading(_velocity, _omega, pose);
      e_distance[v][o] = calcurate_distance(_velocity, _omega, pose);

      sum_heading += e_heading[v][o];
      sum_distance += e_distance[v][o];
    }
    //std::cout << std::endl;
  }
  //正規化
  for(int i=0;i < elements_v;i++){
    for(int j=0;j < elements_o;j++){
      e_heading[i][j] /= sum_heading;
      e_distance[i][j] /= sum_distance;
      //std::cout << e_distance[i][j] << " ";
    }
    //std::cout << std::endl;
  }

  int j = 0;
  int k = 0;
  double max = e[j][k];
  for(int v = 0;v < elements_v;v++){
    for(int o = 0;o < elements_o;o++){
      e[v][o] = ALPHA * e_heading[v][o];
      //std::cout << std::setprecision(4) << e[v][o] << " ";
      double dist_val = BETA *e_distance[v][o];
      if(dist_val == 0.0){
        allowed[v][o] = false;
      }else{
        allowed[v][o] = true;
      }
      //std::cout << std::setprecision(4) << dist_val << " ";
      e[v][o] += dist_val;
      //e[v][o] += GAMMA * calcurate_velocity(_velocity);

      if((e[v][o] > max) && (allowed[v][o])){
        max = e[v][o];
        j=v;
        k=o;
      }
    }
    //std::cout << std::endl;
  }
  velocity.linear.x = (window_down + j * VELOCITY_RESOLUTION);// / MAX_VELOCITY;
  velocity.angular.z = (window_left + k * ANGULAR_VELOCITY_RESOLUTION);// / MAX_ANGULAR_VELOCITY;
  //std::cout << std::endl;
}

double calcurate_heading(double v, double omega, geometry_msgs::Pose& pose)
{
  double _x = pose.position.x;
  double _y = pose.position.y;
  double _theta = get_yaw(pose.orientation);
  double current_theta = get_yaw(current_odometry.pose.pose.orientation);
  double __x = _x * cos(current_theta) - _y * sin(current_theta) + current_odometry.pose.pose.position.x;
  double __y = _x * sin(current_theta) + _y * cos(current_theta) + current_odometry.pose.pose.position.y;
  _theta += current_theta;
  double distance = sqrt(pow(__x - goal.pose.position.x, 2) + pow(__y - goal.pose.position.y, 2));
  //std::cout << std::setprecision(4) << distance << " ";
  double dtheta = fabs(get_yaw(goal.pose.orientation) - _theta);
  double val2 = exp(-dtheta);
  double val = exp(-distance);
  //std::cout << distance << " ";
  return val;//+ val2;
}

double calcurate_distance(double v, double omega, geometry_msgs::Pose& pose)
{
  geometry_msgs::Pose2D object;

  int index = 0;

  double distance = LIMIT_DISTANCE;
  for(int i=20;i<=700;i+=8){//5~175
    if(_laser_data.ranges[i] < LIMIT_DISTANCE){
      object.x = _laser_data.ranges[i] * sin(LASER_RESOLUTION * i);
      object.y = _laser_data.ranges[i] * cos(LASER_RESOLUTION * i) * -1.0;
      double _distance = sqrt(pow((object.x - pose.position.x), 2) + pow((object.y - pose.position.y), 2));
      if(_distance < distance){
        //std::cout << "obj" << object << "pos" << position << std::endl;
        index = i;
        distance = _distance;
      }
      if(distance - ROBOT_RADIUS < 0.0){
        return 0;
      }
    }
  }
  //std::cout << "(" << _laser_data.ranges[index] * sin(LASER_RESOLUTION * index)  << "," << _laser_data.ranges[index] * cos(LASER_RESOLUTION * index) * -1 << ") ";
  //std::cout << v << "[m/s]" << omega << "[rad/s]" << std::endl;
  //std::cout << position << " ";
  //std::cout << distance << " ";
  return distance;
}

double calcurate_velocity(double v)
{
  //std::cout << v << " ";
  return v;
}

void calcurate_dynamic_window(void)
{
  window_left = get_larger(-MAX_ANGULAR_VELOCITY, velocity_odometry.angular.z-MAX_ANGULAR_ACCELERATION*INTERVAL);
  window_up = get_smaller(MAX_VELOCITY, velocity_odometry.linear.x+MAX_ACCELERATION*INTERVAL);
  window_right = get_smaller(MAX_ANGULAR_VELOCITY, velocity_odometry.angular.z+MAX_ANGULAR_ACCELERATION*INTERVAL);
  window_down = get_larger(MIN_VELOCITY, velocity_odometry.linear.x-MAX_ACCELERATION*INTERVAL);

  if(window_left > window_right){
    window_left = -MAX_ANGULAR_VELOCITY;
    window_right = MAX_ANGULAR_VELOCITY;
  }
  if(window_down > window_up){
    window_up = MAX_VELOCITY;
    window_down = MIN_VELOCITY;
  }
}

double get_larger(double a, double b)
{
  if(a > b){
    return a;
  }else{
    return b;
  }
}

double get_smaller(double a, double b)
{
  if(a < b){
    return a;
  }else{
    return b;
  }
}

double get_yaw(geometry_msgs::Quaternion q)
{
  double r, p, y;
  tf::Quaternion quat(q.x, q.y, q.z, q.w);
  tf::Matrix3x3(quat).getRPY(r, p, y);
  return y;
}
