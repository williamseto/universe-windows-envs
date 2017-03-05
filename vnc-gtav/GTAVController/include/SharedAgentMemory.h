#ifndef GTAV_SHARED_AGENT_MEMORY_H_
#define GTAV_SHARED_AGENT_MEMORY_H_


#define AGENT_SHARED_MEMORY TEXT("Local\\GTAVAgent")

struct AgentTime
{
	int year;
	int month;
	int day_of_month;
	int hour;
	int minute;
	int second;
	int ms_per_game_min;
};

struct Vector
{
	double x;
	double y;
	double z;
};

struct TargetInfo
{
	double x;
	double y;
	double z;
	double heading;
	//double speed;
	double vel_x;
	double vel_y;
	double vel_z;

	//Vector fwd_vec;
};

struct SharedAgentMemory
{
	double reward;

	// N.B. ScriptHook driven actions not currently implemented. vJoy used instead.
	double action_throttle; //[0,1]
	double action_brake; //[0,1]
	double action_steer; //[-1,1]. -1 is left, 1 is right

	double x_coord;
	double y_coord;
	double z_coord;
	double dest_x;
	double dest_y;
	double dest_z;
	bool on_road;
	bool should_reset_agent;
	double heading;
	double speed;
	double pitch_velocity;
	double roll_velocity;
	double yaw_velocity;
	double forward_acceleration;
	double lateral_acceleration;
	double vertical_acceleration;
	
	// jariness is measured in g-force seconds over some max comfortable g-force
	double forward_jariness;
	double lateral_jariness;
	double vertical_jariness;

	double velocity_x;
	double velocity_y;
	double velocity_z;
	bool should_game_drive;
	bool is_game_driving;
	int script_hook_loadtime;
	bool use_custom_camera;
	double desired_cam_x_offset;
	double desired_cam_y_offset;
	double desired_cam_z_offset;
	double desired_cam_fov;
	double center_of_lane_reward;
	bool is_colliding;
	unsigned long last_material_collided_with;
	int last_collision_time;
	double distance_from_destination;
	int time_since_drove_against_traffic;
	Vector forward_vector;
	AgentTime time;
	int num_targets;
	TargetInfo targets[8];
	const char * scenario_name; //how much memory will be allocated??
};

#endif // !GTAV_SHARED_AGENT_MEMORY_H_
