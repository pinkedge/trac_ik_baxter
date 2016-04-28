/********************************************************************************
Copyright (c) 2016, TRACLabs, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, 
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation 
       and/or other materials provided with the distribution.

    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software 
       without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.
********************************************************************************/

#include <trac_ik/trac_ik.hpp>
#include <ros/ros.h>
#include <kdl/chainiksolverpos_nr_jl.hpp>
#include <baxter_core_msgs/SolvePositionIK.h>
#include <sensor_msgs/JointState.h>

class BaxterTracIKServer {
private:
    std::string _side;
    double _timeout;
    std::string _urdf_param;
    TRAC_IK::TRAC_IK *_tracik_solver;
    KDL::Chain _chain;
    KDL::JntArray *_nominal;
    ros::ServiceServer _service;
    bool _ready;

public:
    BaxterTracIKServer(std::string side, double timeout, std::string urdf_param) {
        double eps = 1e-5;
        this->_ready = false;
        this->_side = side;
        this->_urdf_param = urdf_param;
        this->_timeout = timeout;
        this->_tracik_solver = new TRAC_IK::TRAC_IK("base", side + "_gripper", urdf_param, timeout, eps);

        KDL::JntArray ll, ul; //lower joint limits, upper joint limits

        if(!(this->_tracik_solver->getKDLChain(this->_chain))) {
          ROS_ERROR("There was no valid KDL chain found");
          return;
        }

        if(!(this->_tracik_solver->getKDLLimits(ll,ul))) {
          ROS_ERROR("There were no valid KDL joint limits found");
          return;
        }

        if(!(this->_chain.getNrOfJoints() == ll.data.size())
           || !(this->_chain.getNrOfJoints() == ul.data.size())) {
            ROS_ERROR("Inconsistent joint limits found");
            return;
        }

        // Create Nominal chain configuration midway between all joint limits
        this->_nominal = new KDL::JntArray(this->_chain.getNrOfJoints());

        for (uint j=0; j < this->_nominal->data.size(); j++) {
          this->_nominal->operator()(j) = (ll(j)+ul(j))/2.0;
        }

        this->_ready = true;
    }

    ~BaxterTracIKServer() {
        delete this->_tracik_solver;
        delete this->_nominal;
    }

    bool perform_ik(baxter_core_msgs::SolvePositionIK::Request &request,
                    baxter_core_msgs::SolvePositionIK::Response &response) {

          int rc;
          KDL::JntArray result;
          sensor_msgs::JointState joint_state;
          for(uint segment=0; segment<this->_chain.getNrOfSegments(); ++segment) {
              KDL::Joint joint = this->_chain.getSegment(segment).getJoint();
              if(joint.getType()!=KDL::Joint::None)
                  joint_state.name.push_back(joint.getName());
          }

          for(uint point=0; point<request.pose_stamp.size(); ++point) {
              KDL::Frame end_effector_pose(KDL::Rotation::Quaternion(request.pose_stamp[point].pose.orientation.x,
                                                                     request.pose_stamp[point].pose.orientation.y,
                                                                     request.pose_stamp[point].pose.orientation.z,
                                                                     request.pose_stamp[point].pose.orientation.w),
                                           KDL::Vector(request.pose_stamp[point].pose.position.x,
                                                       request.pose_stamp[point].pose.position.y,
                                                       request.pose_stamp[point].pose.position.z));

              rc = this->_tracik_solver->CartToJnt(*(this->_nominal), end_effector_pose, result);

              for(uint joint=0; joint<this->_chain.getNrOfJoints(); ++joint) {
                  joint_state.position.push_back(result(joint));
              }

              response.joints.push_back(joint_state);
              response.isValid.push_back(rc>=0);
          }
          return true;
        }
};


int main(int argc, char** argv)
{
  ros::init(argc, argv, "trac_ik_baxter");
  ros::NodeHandle nh;

  std::string urdf_param;
  double timeout;
  
  nh.param("timeout", timeout, 0.005);
  nh.param("urdf_param", urdf_param, std::string("/robot_description"));

  BaxterTracIKServer ik("right", timeout, urdf_param);
  ros::ServiceServer service = nh.advertiseService("compute_trac_ik", &BaxterTracIKServer::perform_ik, &ik);
  ros::spin();

  return 0;
}
