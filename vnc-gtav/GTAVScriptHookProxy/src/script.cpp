#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include "ScriptHookSharedMemory.h"
#include "rewards.h"
#include "script.h"
#include <main.h>
#include <natives.h>
#include <thread>
#include <random>
#include "keyboard.h"
#include "nodes.h"
#include <iostream>
#include <fstream>
#include <math.h>

# define M_PI           3.14159265358979323846

using namespace OpenAIGym;

Nvidia::ScenarioManager s_scenarioManager;

// TODO: Move this into an object
Cam deep_drive_cam = 0;
Cam orig_cam = 0;
std::string status_text;
double curr_cam_x_offset;
double curr_cam_y_offset;
double curr_cam_z_offset;
bool show_debug_status_text = false;
bool first_camera_load = true;
bool shared_mem_initialized = false;
unsigned long last_material_collided_with = 0;
int last_collision_time = 0;
Vector3 last_speed;
bool first_speed_reading = true;
std::chrono::time_point<std::chrono::system_clock> last_speed_measurement_time;
std::chrono::time_point<std::chrono::system_clock> last_reset_time;
Vector3 jariness = { 0, 0, 0 };
int last_time_since_drove_against_traffic = 0;

std::unordered_map<int, tNode> loaded_nodes;



void Script::initializeLogger()
{
	boost::log::add_common_attributes();
    // This file gets created in the root GTAV directory, i.e. `GTA_DIR` in your environment variables.
	boost::log::add_file_log(boost::log::keywords::file_name = "gtav-proxy-log.txt",
		boost::log::keywords::auto_flush = true,
		boost::log::keywords::format = "[%TimeStamp%]: %Message%");

#ifdef _DEBUG
	boost::log::core::get()->set_filter(boost::log::trivial::severity >= boost::log::trivial::debug);
#else
	boost::log::core::get()->set_filter(boost::log::trivial::severity >= boost::log::trivial::info);
#endif

}

void update_status_text()
{
	UI::SET_TEXT_FONT(0);
	UI::SET_TEXT_SCALE(0.55, 0.55);
	UI::SET_TEXT_COLOUR(255, 255, 255, 255);
	UI::SET_TEXT_WRAP(0.0, 1.0);
	UI::SET_TEXT_CENTRE(1);
	UI::SET_TEXT_DROPSHADOW(0, 0, 0, 0, 0);
	UI::SET_TEXT_EDGE(1, 0, 0, 0, 205);
	UI::_SET_TEXT_ENTRY("STRING");
	UI::_ADD_TEXT_COMPONENT_STRING((char *)status_text.c_str());
	UI::_DRAW_TEXT(0.5, 0.5);
}

void Script::set_camera(const int vehicle, SharedAgentMemory* shared, const int curr_vehicle)
{
	if(vehicle == 0)
	{
		BOOST_LOG_TRIVIAL(info) << "No vehicle detected, reverting to original camera";
		CAM::SET_CAM_ACTIVE(orig_cam, TRUE);
		CAM::RENDER_SCRIPT_CAMS(TRUE, FALSE, orig_cam, TRUE, FALSE);
	}
	else if(first_camera_load || curr_vehicle != vehicle || (
		shared->desired_cam_x_offset != curr_cam_x_offset ||
		shared->desired_cam_y_offset != curr_cam_y_offset ||
		shared->desired_cam_z_offset != curr_cam_z_offset
	))
	{
		BOOST_LOG_TRIVIAL(info) << "Creating new camera";
		CAM::DESTROY_ALL_CAMS(true);
		auto rotation = ENTITY::GET_ENTITY_ROTATION(vehicle, FALSE);
		auto position = ENTITY::GET_ENTITY_COORDS(vehicle, FALSE);
		deep_drive_cam = CAM::CREATE_CAM_WITH_PARAMS("DEFAULT_SCRIPTED_FLY_CAMERA", position.x, position.y, position.z, 0.0, 0.0, 0.0, 50.0, 0, 2);
		CAM::ATTACH_CAM_TO_ENTITY(deep_drive_cam, vehicle, shared->desired_cam_x_offset, shared->desired_cam_y_offset, shared->desired_cam_z_offset, TRUE);
		CAM::SET_CAM_FOV(deep_drive_cam, shared->desired_cam_fov);
		CAM::SET_CAM_ROT(deep_drive_cam, rotation.x, rotation.y, rotation.z, 2);
		CAM::SET_CAM_ACTIVE(deep_drive_cam, TRUE);
		CAM::RENDER_SCRIPT_CAMS(TRUE, FALSE, deep_drive_cam, TRUE, FALSE);
		first_camera_load = false;
	}
}

void Script::refresh(Player& player, Ped& player_ped, int& vehicle, SharedAgentMemory* shared)
{
	// Avoid getting car-jacked, police chasing us, etc...

	int curr_vehicle = vehicle;
	player = PLAYER::PLAYER_ID();
	player_ped = PLAYER::PLAYER_PED_ID();
	vehicle = PED::GET_VEHICLE_PED_IS_USING(player_ped);
	PLAYER::SET_EVERYONE_IGNORE_PLAYER(player, true);
	PLAYER::SET_POLICE_IGNORE_PLAYER(player, true);
	PLAYER::CLEAR_PLAYER_WANTED_LEVEL(player); // Never wanted

	// Put on seat belt
	const int PED_FLAG_CAN_FLY_THRU_WINDSCREEN = 32;
	if (PED::GET_PED_CONFIG_FLAG(player_ped, PED_FLAG_CAN_FLY_THRU_WINDSCREEN, TRUE))
	{
		PED::SET_PED_CONFIG_FLAG(player_ped, PED_FLAG_CAN_FLY_THRU_WINDSCREEN, FALSE);
	}

	// Invincible vehicle
	VEHICLE::SET_VEHICLE_TYRES_CAN_BURST(vehicle, FALSE);
	VEHICLE::SET_VEHICLE_WHEELS_CAN_BREAK(vehicle, FALSE);
	VEHICLE::SET_VEHICLE_HAS_STRONG_AXLES(vehicle, TRUE);

	VEHICLE::SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED(vehicle, FALSE);
	ENTITY::SET_ENTITY_INVINCIBLE(vehicle, TRUE);
	ENTITY::SET_ENTITY_PROOFS(vehicle, 1, 1, 1, 1, 1, 1, 1, 1);

	// Player invinsible
	PLAYER::SET_PLAYER_INVINCIBLE(player, TRUE);

	// Driving characteristics
	PED::SET_DRIVER_AGGRESSIVENESS(player_ped, 0.0);
	PED::SET_DRIVER_ABILITY(player_ped, 1.0);

	if(shared->use_custom_camera)
	{
		BOOST_LOG_TRIVIAL(info) << "Setting custom camera";
		set_camera(vehicle, shared, curr_vehicle);
	}

}

std::random_device random_seed;     // only used once to initialise (seed) engine
std::mt19937 random_generator(random_seed());    // random-number engine used (Mersenne-Twister in this case)

template<typename Iter, typename RandomGenerator>
Iter select_randomly(Iter start, Iter end, RandomGenerator& g) {
	std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
	std::advance(start, dis(g));
	return start;
}

template<typename Iter>
Iter select_randomly(Iter start, Iter end) {
	static std::random_device rd;
	return select_randomly(start, end, random_generator);
}

inline double get_random_double(double start, double end)
{
	std::uniform_real_distribution<double> uni(start, end); // guaranteed unbiased
	auto random_double = uni(random_generator);
	return random_double;
}
// Returns 1 if the lines intersect, otherwise 0. In addition, if the lines 
// intersect the intersection point may be stored in the floats i_x and i_y.
inline int get_line_intersection(float p0_x, float p0_y, float p1_x, float p1_y,
	float p2_x, float p2_y, float p3_x, float p3_y, float *i_x, float *i_y)
{
	float s1_x, s1_y, s2_x, s2_y;
	s1_x = p1_x - p0_x;     s1_y = p1_y - p0_y;
	s2_x = p3_x - p2_x;     s2_y = p3_y - p2_y;

	float s, t;
	s = (-s1_y * (p0_x - p2_x) + s1_x * (p0_y - p2_y)) / (-s2_x * s1_y + s1_x * s2_y);
	t = (s2_x * (p0_y - p2_y) - s2_y * (p0_x - p2_x)) / (-s2_x * s1_y + s1_x * s2_y);

	if (s >= 0 && s <= 1 && t >= 0 && t <= 1)
	{
		// Collision detected
		if (i_x != NULL)
			*i_x = p0_x + (t * s1_x);
		if (i_y != NULL)
			*i_y = p0_y + (t * s1_y);
		return 1;
	}

	return 0; // No collision
}

void Script::perform_temp_vehicle_action(Ped player_ped, int vehicle, SharedAgentMemory* shared)
{
	// 1, 7, 8, 23
	// Actions:
	//'1 - brake
	//'3 - brake + reverse
	//'4 - turn left 90 + braking
	//'5 - turn right 90 + braking
	//'6 - brake strong (handbrake?) until time ends
	//'7 - turn left + accelerate
	//'7 - turn right + accelerate
	//'9 - weak acceleration
	//'10 - turn left + restore wheel pos to center in the end
	//'11 - turn right + restore wheel pos to center in the end
	//'13 - turn left + go reverse
	//'14 - turn left + go reverse
	//'16 - crash the game after like 2 seconds :)
	//'17 - keep actual state, game crashed after few tries
	//'18 - game crash
	//'19 - strong brake + turn left/right
	//'20 - weak brake + turn left then turn right
	//'21 - weak brake + turn right then turn left
	//'22 - brake + reverse
	//'23 - accelerate fast
	//'24 - brake
	//'25 - brake turning left then when almost stopping it turns left more
	//'26 - brake turning right then when almost stopping it turns right more
	//'27 - brake until car stop or until time ends
	//'28 - brake + strong reverse acceleration
	//'30 - performs a burnout (brake until stop + brake and accelerate)
	//'31 - accelerate in neutral
	//'32 - accelerate very strong

	int time = 100;
	std::vector<int> temp_actions({
		1, // brake
		7, // right + accelerate
		8, // left + accelerate
		23 // strong accelerate
	});
	int action = *select_randomly(temp_actions.begin(), temp_actions.end());
	AI::TASK_VEHICLE_TEMP_ACTION(player_ped, vehicle, action, time);
}

void Script::detect_if_in_vehicle(int vehicle)
{
	if(vehicle == 0)
	{
		status_text = "get a ride";
	}
}

void Script::handle_artificial_demonstration_switching(SharedAgentMemory* shared, Ped player_ped, int vehicle, bool is_game_driving)
{
	if( ! is_game_driving && shared->should_game_drive)
	{
		AI::TASK_VEHICLE_DRIVE_WANDER(player_ped, vehicle, 10.0, 786603);
		ScriptHookSharedMemory::shared()->is_game_driving = true;
		is_game_driving = true;
		shared->should_game_drive = true;
	}
	else if(is_game_driving && !shared->should_game_drive)
	{
		shared->is_game_driving = false;
		is_game_driving = false;
		shared->should_game_drive = false;
		// TODO: Verify this gives us back control of the car (it seems to have been intermittent in the past, but we may have to respawn in the driver's seat to take back control)

		perform_temp_vehicle_action(player_ped, vehicle, shared);
	}
}

void Script::add_debug_status_text(std::string text)
{
	if(show_debug_status_text)
	{
		status_text += text + "\n";
	}

}

void Script::get_acceleration(Vector3 speed, Vector3& acceleration, Vector3& jariness)
{
	if (first_speed_reading)
	{
		first_speed_reading = false;
		jariness = { 0, 0, 0 };
	}
	else {
		std::chrono::time_point<std::chrono::system_clock> current_time = std::chrono::steady_clock::now();
		std::chrono::duration<double> elapsed_seconds = current_time - last_speed_measurement_time;
		double seconds = static_cast<double>(elapsed_seconds.count());
		acceleration.x = (speed.x - last_speed.x) / seconds;
		acceleration.y = (speed.y - last_speed.y) / seconds;
		acceleration.z = (speed.z - last_speed.z) / seconds;

		double gforce_seconds = seconds / 9.8;

		// https://www.quora.com/Hyperloop-What-is-a-physically-comfortable-rate-of-acceleration-for-human-beings
		double max_comfortable_accleration = 5; // roughly 0.5 g's

		if (abs(acceleration.x) > max_comfortable_accleration)
		{
			jariness.x += abs(acceleration.x) * gforce_seconds;
		}
		if (abs(acceleration.y) > max_comfortable_accleration)
		{
			jariness.y += abs(acceleration.y) * gforce_seconds;
		}
		if (abs(acceleration.z) > max_comfortable_accleration)
		{
			jariness.z += abs(acceleration.z) * gforce_seconds;
		}
	}
	last_speed = speed;
	last_speed_measurement_time = std::chrono::steady_clock::now();
}

void Script::get_time_since_drove_against_traffic(int player, int& time_since_drove_against_traffic)
{
	time_since_drove_against_traffic = PLAYER::GET_TIME_SINCE_PLAYER_DROVE_AGAINST_TRAFFIC(player);
	std::chrono::time_point<std::chrono::system_clock> current_time = std::chrono::steady_clock::now();
	std::chrono::duration<double> time_since_reset = current_time - last_reset_time;
	add_debug_status_text("last reset time: " + std::to_string(time_since_reset.count()));

	// make sure that we don't reset this value too fast in case we're in the middle of resetting
	// game returns time in milliseconds
	//if (time_since_drove_against_traffic > (time_since_reset.count() + 10) * 1000)
	if (time_since_drove_against_traffic > (time_since_reset.count() + 1) * 1000)
	{
		// time since drove against traffic has not changed since reset
		time_since_drove_against_traffic = 0;
	}
}

void Script::set_reward_and_info_shared_mem(SharedAgentMemory* shared, int player, int vehicle)
{
	Vector3 speed = ENTITY::GET_ENTITY_SPEED_VECTOR(vehicle, true);
	auto spin = ENTITY::GET_ENTITY_ROTATION_VELOCITY(vehicle);
	double heading = ENTITY::GET_ENTITY_HEADING(vehicle);
	Vector3 veh_coords = ENTITY::GET_ENTITY_COORDS(vehicle, true);
	bool point_on_road = PATHFIND::IS_POINT_ON_ROAD(veh_coords.x, veh_coords.y, veh_coords.z, vehicle);

	int time_since_drove_against_traffic;
	get_time_since_drove_against_traffic(player, time_since_drove_against_traffic);

	Vector3 acceleration;
	get_acceleration(speed, acceleration, jariness);

	// TODO: Enable directly setting actions (and avoiding Vjoy) by setting actions in Env.act and reading here
	// TODONT?: Can we perform all actions (honk horn, flip off :) )? Also, vJoy allows easily adding other PC games...
	//	        Problem is more painful setup and needing to start vJoy Feeder to clear out queues and regain non-buggy manual control.

	//Performs the actions
//	CONTROLS::_SET_CONTROL_NORMAL(27, 59, shared->action_steer);
//	CONTROLS::_SET_CONTROL_NORMAL(27, 71, shared->action_throttle);
//	CONTROLS::_SET_CONTROL_NORMAL(27, 72, shared->action_brake);

	Vector3 position = ENTITY::GET_ENTITY_COORDS(vehicle, FALSE);
	Vector3 forward_vector_ = ENTITY::GET_ENTITY_FORWARD_VECTOR(vehicle);
	Vector forward_vector;
	forward_vector.x = forward_vector_.x;
	forward_vector.y = forward_vector_.y;
	forward_vector.z = forward_vector_.z;

	// TODO: Load paths.xml and populate nodes in background thread to avoid hanging (and crashing on slow systems) the game.
	//double center_of_lane_reward = rewardCenterOfLane(position, forward_vector);
	double center_of_lane_reward = 0.0;

	auto _last_material_collided_with = ENTITY::GET_LAST_MATERIAL_HIT_BY_ENTITY(vehicle);
	if (_last_material_collided_with != 0)
	{
		last_material_collided_with = _last_material_collided_with;
	}
	bool is_colliding = ENTITY::HAS_ENTITY_COLLIDED_WITH_ANYTHING(vehicle);
	if (is_colliding)
	{
		last_collision_time = int(time(NULL));
	}

	std::tm time;
	TIME::GET_LOCAL_TIME(&time.tm_year, &time.tm_mon, &time.tm_mday, &time.tm_hour, &time.tm_min, &time.tm_sec);
	int second = TIME::GET_CLOCK_SECONDS();
	int minute = TIME::GET_CLOCK_MINUTES();
	int hour = TIME::GET_CLOCK_HOURS();
	int day_of_month = TIME::GET_CLOCK_DAY_OF_MONTH();
	int month = TIME::GET_CLOCK_MONTH();
	int year = TIME::GET_CLOCK_YEAR();
	int ms_per_game_min = TIME::GET_MILLISECONDS_PER_GAME_MINUTE();

	float distance_from_destination = PATHFIND::CALCULATE_TRAVEL_DISTANCE_BETWEEN_POINTS(
		shared->x_coord, shared->y_coord, shared->z_coord, shared->dest_x, shared->dest_y, shared->dest_z);

	add_debug_status_text("distance_from_destination: " + std::to_string(distance_from_destination));
	add_debug_status_text("last collision time: " + std::to_string(last_collision_time));
	add_debug_status_text("last drove against traffic time: " + std::to_string(time_since_drove_against_traffic));
//	add_debug_status_text("second: " + std::to_string(second));
//	add_debug_status_text("camera x: " + std::to_string(shared->desired_cam_x_offset));
//	add_debug_status_text("camera y: " + std::to_string(shared->desired_cam_y_offset));
//	add_debug_status_text("camera z: " + std::to_string(shared->desired_cam_z_offset));
//	add_debug_status_text("use custom camera: " + std::to_string(shared->use_custom_camera));
//	add_debug_status_text("is colliding: " + std::to_string(is_colliding));
//	add_debug_status_text("acceleration: " + std::to_string(acceleration.y));
//	add_debug_status_text("lateral accl: " + std::to_string(acceleration.x));
//	add_debug_status_text("vert accl: "    + std::to_string(acceleration.z));
	add_debug_status_text("forward jariness: "    + std::to_string(jariness.y));
	add_debug_status_text("lateral jariness: "    + std::to_string(jariness.x));
	add_debug_status_text("vertical jariness: "   + std::to_string(jariness.z));
//	add_debug_status_text("lane reward: " + std::to_string(center_of_lane_reward));

	shared->reward                           = 0.0; // rewardFunction(vehicle, 23.0, 0.5);
	shared->x_coord                          = veh_coords.x;
	shared->y_coord                          = veh_coords.y;
	shared->z_coord                          = veh_coords.z;
	shared->on_road                          = point_on_road;
	shared->pitch_velocity                   = spin.x;
	shared->roll_velocity                    = spin.y;
	shared->yaw_velocity                     = spin.z;
	shared->forward_acceleration             = acceleration.y;
	shared->lateral_acceleration             = acceleration.x;
	shared->vertical_acceleration            = acceleration.z;
	shared->forward_jariness                 = jariness.y;
	shared->lateral_jariness                 = jariness.x;
	shared->vertical_jariness                = jariness.x;
	shared->heading                          = heading;
	shared->speed                            = speed.y;
	shared->velocity_x                       = speed.x;
	shared->velocity_y                       = speed.y;
	shared->velocity_z                       = speed.z;
	shared->center_of_lane_reward            = center_of_lane_reward;
	shared->is_colliding                     = is_colliding;
	shared->last_material_collided_with      = last_material_collided_with;
	shared->last_collision_time              = last_collision_time;
	shared->time.second                      = second;
	shared->time.minute                      = minute;
	shared->time.hour                        = hour;
	shared->time.day_of_month                = day_of_month;
	shared->time.month                       = month;
	shared->time.year                        = year;
	shared->time.ms_per_game_min             = ms_per_game_min;
	shared->forward_vector                   = forward_vector;
	shared->distance_from_destination        = distance_from_destination;
	shared->time_since_drove_against_traffic = time_since_drove_against_traffic;

}

inline void draw_intersection(Vector3 playerCoord, Vector3 targetCoord)
{
	targetCoord.x = 1718; targetCoord.y = 3645; targetCoord.z = 35.06;
	//playerCoord.x = 1805.271; playerCoord.y = 3656.37; playerCoord.z = 33.88629;
	playerCoord.x = 1784.779; playerCoord.y = 3687.421; playerCoord.z = 33.85;

	tNode node;
	tLink link;

	// get some number of nodes. nodes are basically locations denoting a point on the road


	tLinkDir playerLinkDir;
	tLinkDir targetLinkDir;
	for (int i = 1; i <= 20; i++) {
		int nodeID = PATHFIND::GET_NTH_CLOSEST_VEHICLE_NODE_ID(playerCoord.x, playerCoord.y, playerCoord.z, i, 0, 300, 300);
		node = loaded_nodes[nodeID];


		bool found = false;
		//link points are perpendicular to direction of road
		//to get the actual lanes, we will need to organize them by direction
		for (int j = 0; j < node.links.size(); j++) {
			link = node.links.at(j);

			//if (link.attr.shortcut || link.attr.width == -1) continue;

			// checking linepoint size condition since first point is usually the middle of the "link" (on the road)
			// the next two points are on opposite sides of the road
			if (link.attr.shortcut || link.attr.width == -1 || link.linePoints.size() != 3) continue;

			//BOOST_LOG_TRIVIAL(info) << "link coord: " << link.coord.x << ", " << link.coord.y << ", " << link.coord.z << " link direction: " << link.direction.x << ", " << link.direction.y << ", " << link.direction.z;


			playerLinkDir.coord1 = link.linePoints.at(1).coord;
			playerLinkDir.direction = link.direction;
			playerLinkDir.coord2 = link.linePoints.at(2).coord;
			found = true;

			//BOOST_LOG_TRIVIAL(info) << "link (road) width: " << SYSTEM::VDIST(playerLinkDir.coord1.x, playerLinkDir.coord1.y, 0, playerLinkDir.coord2.x, playerLinkDir.coord2.y, 0);

			//GRAPHICS::DRAW_LINE(link.coord.x, link.coord.y, link.coord.z, link.coord.x, link.coord.y, link.coord.z + 3, 255, 0, 0, 255);
			GRAPHICS::DRAW_LINE(playerLinkDir.coord1.x, playerLinkDir.coord1.y, playerLinkDir.coord1.z, playerLinkDir.coord2.x, playerLinkDir.coord2.y, playerLinkDir.coord2.z, 255, 0, 0, 255);

			//if (bunchofpoints.size() > 0)
			//{
			//	float bestDist = 99999;
			//	Vector3 bestPoint;
			//	double bestAngle;
			//	int bi = -1;
			//	for (int i = 0; i < bunchofpoints.size(); i++)
			//	{
			//		float diff_x = node.coord.x - bunchofpoints[i].x;
			//		float diff_y = node.coord.y - bunchofpoints[i].y;

			//		// only consider points which are about the same direction
			//		double unscaled_dotp = link.direction.x*diff_x + link.direction.y*diff_y;
			//		double tmpAngle = acos(unscaled_dotp / (sqrt(pow(diff_x, 2) + pow(diff_y, 2))));

			//		// 10 degree tolerance
			//		if (abs(tmpAngle) < (40 * M_PI / 180))
			//		{
			//			float currDist = SYSTEM::VDIST(bunchofpoints[i].x, bunchofpoints[i].y, 0, node.coord.x, node.coord.y, 0);
			//			if (currDist < bestDist)
			//			{
			//				bestDist = currDist;
			//				bestPoint = bunchofpoints[i];
			//				bestAngle = tmpAngle;
			//				bi = i;
			//			}
			//		}
			//	}
			//	//GRAPHICS::DRAW_LINE(node.coord.x, node.coord.y, node.coord.z + 1, bestPoint.x, bestPoint.y, bestPoint.z + 1, 255, 0, 0, 255);

			//	if (bi != -1)
			//	{
			//		GRAPHICS::DRAW_LINE(link.linePoints.at(1).coord.x, link.linePoints.at(1).coord.y, link.linePoints.at(1).coord.z + 1, bunchofpoints1[bi].x, bunchofpoints1[bi].y, bunchofpoints1[bi].z + 1, 255, 0, 0, 255);
			//		GRAPHICS::DRAW_LINE(link.linePoints.at(2).coord.x, link.linePoints.at(2).coord.y, link.linePoints.at(2).coord.z + 1, bunchofpoints2[bi].x, bunchofpoints2[bi].y, bunchofpoints2[bi].z + 1, 255, 0, 0, 255);
			//	}
			//	//BOOST_LOG_TRIVIAL(info) << "best angle: " << bestAngle;
			//}

			//// should we break or not? empirically it looks like multiple links are usually almost the same
			//break;
		}
		//if (found)
		//{
		//	break;
		//}
	}

	//for (int i = 1; i <= 6; i++) {
	//	int nodeID = PATHFIND::GET_NTH_CLOSEST_VEHICLE_NODE_ID(targetCoord.x, targetCoord.y, targetCoord.z, i, 0, 300, 300);
	//	node = loaded_nodes[nodeID];
	//	bool found = false;

	//	for (int j = 0; j < node.links.size(); j++) {
	//		link = node.links.at(j);

	//		if (link.attr.shortcut || link.attr.width == -1 || link.linePoints.size() != 3) continue;

	//		targetLinkDir.coord1 = link.linePoints.at(1).coord;
	//		targetLinkDir.direction = link.direction;
	//		targetLinkDir.coord2 = link.linePoints.at(2).coord;
	//		found = true;

	//		GRAPHICS::DRAW_LINE(link.coord.x, link.coord.y, link.coord.z, link.coord.x, link.coord.y, link.coord.z + 3, 255, 0, 0, 255);

	//		break;
	//	}
	//	if (found)
	//	{
	//		break;
	//	}
	//}

	//Vector3 playerCoord3;
	//playerCoord3.x = playerLinkDir.coord1.x - playerLinkDir.direction.x * 100; playerCoord3.y = playerLinkDir.coord1.y - playerLinkDir.direction.y * 100;
	////GRAPHICS::DRAW_LINE(playerLinkDir.coord1.x, playerLinkDir.coord1.y, playerLinkDir.coord1.z + 1, playerCoord3.x, playerCoord3.y, playerLinkDir.coord1.z + 1, 255, 0, 0, 255);
	//Vector3 playerCoord4;
	//playerCoord4.x = playerLinkDir.coord2.x - playerLinkDir.direction.x * 100; playerCoord4.y = playerLinkDir.coord2.y - playerLinkDir.direction.y * 100;
	////GRAPHICS::DRAW_LINE(playerLinkDir.coord2.x, playerLinkDir.coord2.y, playerLinkDir.coord2.z + 1, playerCoord4.x, playerCoord4.y, playerLinkDir.coord2.z + 1, 255, 0, 0, 255);

	//Vector3 targetCoord3;
	//targetCoord3.x = targetLinkDir.coord1.x + targetLinkDir.direction.x * 100; targetCoord3.y = targetLinkDir.coord1.y + targetLinkDir.direction.y * 100;
	////GRAPHICS::DRAW_LINE(targetLinkDir.coord1.x, targetLinkDir.coord1.y, targetLinkDir.coord1.z + 1, targetCoord3.x, targetCoord3.y, targetLinkDir.coord1.z + 1, 255, 0, 0, 255);
	//Vector3 targetCoord4;
	//targetCoord4.x = targetLinkDir.coord2.x + targetLinkDir.direction.x * 100; targetCoord4.y = targetLinkDir.coord2.y + targetLinkDir.direction.y * 100;
	////GRAPHICS::DRAW_LINE(targetLinkDir.coord2.x, targetLinkDir.coord2.y, targetLinkDir.coord2.z + 1, targetCoord4.x, targetCoord4.y, targetLinkDir.coord2.z + 1, 255, 0, 0, 255);

	//float int_x, int_y;
	//get_line_intersection(playerLinkDir.coord1.x, playerLinkDir.coord1.y, playerCoord3.x, playerCoord3.y,
	//	targetLinkDir.coord1.x, targetLinkDir.coord1.y, targetCoord3.x, targetCoord3.y, &int_x, &int_y);
	//GRAPHICS::DRAW_LINE(int_x, int_y, targetLinkDir.coord1.z, int_x, int_y, targetLinkDir.coord1.z + 3, 255, 0, 0, 255);

	//GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, targetLinkDir.coord1.x, targetLinkDir.coord1.y, playerCoord.z, 255, 0, 0, 255);
	////GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, playerCoord3.x, playerCoord3.y, playerCoord.z, 255, 0, 0, 255);
	//GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, playerLinkDir.coord1.x, playerLinkDir.coord1.y, playerCoord.z, 255, 0, 0, 255);

	//get_line_intersection(playerLinkDir.coord1.x, playerLinkDir.coord1.y, playerCoord3.x, playerCoord3.y,
	//	targetLinkDir.coord2.x, targetLinkDir.coord2.y, targetCoord4.x, targetCoord4.y, &int_x, &int_y);
	//GRAPHICS::DRAW_LINE(int_x, int_y, targetLinkDir.coord1.z, int_x, int_y, targetLinkDir.coord1.z + 3, 255, 0, 0, 255);

	//GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, targetLinkDir.coord2.x, targetLinkDir.coord2.y, playerCoord.z, 255, 0, 0, 255);
	////GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, playerLinkDir.coord1.x, playerLinkDir.coord1.y, playerCoord.z, 255, 0, 0, 255);
	//GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, playerCoord3.x, playerCoord3.y, playerCoord.z, 255, 0, 0, 255);

	//get_line_intersection(playerLinkDir.coord2.x, playerLinkDir.coord2.y, playerCoord4.x, playerCoord4.y,
	//	targetLinkDir.coord1.x, targetLinkDir.coord1.y, targetCoord3.x, targetCoord3.y, &int_x, &int_y);
	//GRAPHICS::DRAW_LINE(int_x, int_y, targetLinkDir.coord1.z, int_x, int_y, targetLinkDir.coord1.z + 3, 255, 0, 0, 255);

	////GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, playerCoord4.x, playerCoord4.y, playerCoord.z, 255, 0, 0, 255);
	//GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, playerLinkDir.coord2.x, playerLinkDir.coord2.y, playerCoord.z, 255, 0, 0, 255);
	//GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, targetCoord3.x, targetCoord3.y, playerCoord.z, 255, 0, 0, 255);

	//get_line_intersection(playerLinkDir.coord2.x, playerLinkDir.coord2.y, playerCoord4.x, playerCoord4.y,
	//	targetLinkDir.coord2.x, targetLinkDir.coord2.y, targetCoord4.x, targetCoord4.y, &int_x, &int_y);
	//GRAPHICS::DRAW_LINE(int_x, int_y, targetLinkDir.coord1.z, int_x, int_y, targetLinkDir.coord1.z + 3, 255, 0, 0, 255);

	////GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, playerLinkDir.coord2.x, playerLinkDir.coord2.y, playerCoord.z, 255, 0, 0, 255);
	//GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, playerCoord4.x, playerCoord4.y, playerCoord.z, 255, 0, 0, 255);
	//GRAPHICS::DRAW_LINE(int_x, int_y, playerCoord.z, targetCoord4.x, targetCoord4.y, playerCoord.z, 255, 0, 0, 255);


}


#define sind(x) (sin(fmod((x),360) * M_PI / 180))
#define cosd(x) (cos(fmod((x),360) * M_PI / 180))

void set_scan_info(SharedAgentMemory* shared, int playerVehicle)
{
	float startX, startY, startZ, targetX, targetY, targetZ;

	// start position of the raycast
	startX = shared->x_coord; startY = shared->y_coord; startZ = shared->z_coord + 0.5;

	targetZ = startZ;
	int vehicleHeading = (int)shared->heading;

	Vector3 hitPositionArg;
	BOOL hitSomethingArg;
	int entityHandleArg;
	Vector3 surfaceNormalArg;

	int scan_count = 0;
	for (int deg = -20 + vehicleHeading; deg <= 200 + vehicleHeading; deg += 10)
	{
		double rad = deg * M_PI / 180;

		targetX = startX + cosd(deg) * 80;
		targetY = startY + sind(deg) * 80;

		int raycastHandle = WORLDPROBE::_CAST_RAY_POINT_TO_POINT(startX, startY, startZ, targetX, targetY, targetZ, -1, playerVehicle, 7);

		int raycastResult = WORLDPROBE::_GET_RAYCAST_RESULT(raycastHandle, &hitSomethingArg, &hitPositionArg, &surfaceNormalArg, &entityHandleArg);

		if (hitSomethingArg)
		{
			shared->scan_info[scan_count].x = hitPositionArg.x;
			shared->scan_info[scan_count].y = hitPositionArg.y;
			shared->scan_info[scan_count].z = hitPositionArg.z;
		}
		else
		{
			shared->scan_info[scan_count].x = targetX;
			shared->scan_info[scan_count].y = targetY;
			shared->scan_info[scan_count].z = targetZ;
		}
		scan_count++;
	}
	shared->num_scans = scan_count;
}

void set_scenario_dest_shared_mem(SharedAgentMemory* shared, Vector3 destination)
{
	shared->use_scenario_dest = true;
	shared->dest_x = destination.x;
	shared->dest_y = destination.y;
	shared->dest_z = destination.z;
}

void Script::set_target_info_shared_mem(SharedAgentMemory* shared, const std::vector<int>& actors, int playerVehicle)
{
	//int playerID = PLAYER::PLAYER_ID();
	//int player_pedID = PLAYER::PLAYER_PED_ID();
	//int playerVehicleID = PED::GET_VEHICLE_PED_IS_USING(player_pedID);

	shared->num_targets = (int) (actors.size() - 1); //1 is ourself (player)

	int idx = 0;
	for (int i = 0; i < actors.size(); i++)
	{

		// ToDo: don't add player info
		if (actors[i] != playerVehicle)
		{
			Vector3 position = ENTITY::GET_ENTITY_COORDS(actors[i], FALSE);

			//true means this is relative to the frame of the actor
			Vector3 speed = ENTITY::GET_ENTITY_SPEED_VECTOR(actors[i], true);

			//auto spin = ENTITY::GET_ENTITY_ROTATION_VELOCITY(vehicle);
			double heading = ENTITY::GET_ENTITY_HEADING(actors[i]);

			shared->targets[idx].x= position.x;
			shared->targets[idx].y = position.y;
			shared->targets[idx].z = position.z;
			shared->targets[idx].heading = heading;
			shared->targets[idx].vel_x = speed.x;
			shared->targets[idx].vel_y = speed.y;
			shared->targets[idx].vel_z = speed.z;
			idx++;

			// debug intersection node locations
			draw_intersection(ENTITY::GET_ENTITY_COORDS(playerVehicle, FALSE), position);
		}
	}
}

void Script::display_loading_paths_message()
{
	for(int i = 0; i < 100; i++)
	{
		status_text = "Loading paths.xml, screen will freeze for \nabout 15 seconds...";
		update_status_text();
		WAIT(10);
	}
}

void Script::deep_drive()
{
	BOOST_LOG_TRIVIAL(info) << "Starting deep drive";
	int iter = 0;
	SharedAgentMemory* shared = ScriptHookSharedMemory::shared();
	Player player;
	Ped player_ped;
	int vehicle = -1;
	bool is_game_driving = false;
	orig_cam = CAM::GET_RENDERING_CAM();
	std::chrono::time_point<std::chrono::system_clock> start_time = std::chrono::system_clock::now();
	while (true)
	{	

		if(iter % 100 == 0) // Do this every second as the game seems to unset certain mod settings every once in a while
		{
			BOOST_LOG_TRIVIAL(info) << "Refreshing mod settings...";
			refresh(player, player_ped, vehicle, shared);
			detect_if_in_vehicle(vehicle);
			BOOST_LOG_TRIVIAL(info) << "Done refreshing mod settings";
		}

		if(vehicle > 0)
		{
			status_text = "";
			handle_artificial_demonstration_switching(shared, player_ped, vehicle, is_game_driving); 

			//if(pathsNotYetLoaded())  // TODO: Fix path loading crash on slow machines like AWS
			//{
			//	display_loading_paths_message();
			//}

			if(isKeyJustUp(VK_F10))
			{
				BOOST_LOG_TRIVIAL(info) << "Toggling debug status text";
				show_debug_status_text = ! show_debug_status_text;
			}
			if (shared->should_reset_agent || isKeyJustUp(VK_F11))
			{
				BOOST_LOG_TRIVIAL(info) << "Resetting agent";
				reset_agent(shared);
				shared->should_reset_agent = false;
			}

			set_reward_and_info_shared_mem(shared, player, vehicle);

			if(shared->use_custom_camera)
			{
				auto rotation = ENTITY::GET_ENTITY_ROTATION(vehicle, FALSE);
				CAM::SET_CAM_ROT(deep_drive_cam, rotation.x, rotation.y, rotation.z, 2);				
			}
		}

		WAIT(10);
		update_status_text();
		iter++;
	}
}


void Script::reset_agent(SharedAgentMemory* shared)
{
	BOOST_LOG_TRIVIAL(info) << "Before setting script load time";
	auto now = time(NULL);
	BOOST_LOG_TRIVIAL(info) << "Setting scripthook load time to " << now;
	(*shared).script_hook_loadtime = int(now);
	BOOST_LOG_TRIVIAL(info) << "Set scripthook load time";
	(*shared).use_custom_camera = false;
	(*shared).use_agent_actions = false;

	//	// Center mirror
	//	(*agent_data)->desired_cam_x_offset = 0;
	//	(*agent_data)->desired_cam_y_offset = 0.3;
	//	(*agent_data)->desired_cam_z_offset = 0.4;

	//// Center hood
	//(*agent_data)->desired_cam_x_offset = 0;
	//(*agent_data)->desired_cam_y_offset = 0.5;
	//(*agent_data)->desired_cam_z_offset = 0.4;

	// Slight left top
	(*shared).desired_cam_x_offset = -0.312;
	(*shared).desired_cam_y_offset = 1.;
	(*shared).desired_cam_z_offset = 0.7;
   	
	BOOST_LOG_TRIVIAL(info) << "Set initial camera offsets";

	(*shared).desired_cam_fov = 92.3;
	(*shared).distance_from_destination = -1;

	last_time_since_drove_against_traffic = shared->time_since_drove_against_traffic;
	(*shared).time_since_drove_against_traffic = 0;

	last_material_collided_with = 0;
	last_collision_time = 0;
	last_speed = {0, 0, 0};
	first_speed_reading = true;
	last_speed_measurement_time = std::chrono::steady_clock::now();
	last_reset_time = std::chrono::steady_clock::now();
	jariness = { 0, 0, 0 };

	(*shared).last_collision_time = 0;
	(*shared).forward_jariness = 0;
	(*shared).lateral_jariness = 0;
	(*shared).vertical_jariness = 0;

}

void Script::main()
{
	initializeLogger();
	BOOST_LOG_TRIVIAL(info) << "Allocating shared memory";
	ScriptHookSharedMemory::allocate_shared_agent_memory();
	SharedAgentMemory* shared = ScriptHookSharedMemory::shared();
	reset_agent(shared);

	Player player;
	Ped player_ped;
	int vehicle = -1;
	int iter = 0;

#ifdef USE_NVIDIA_SCENARIOS

	// wait until story mode is loaded
	while (true)
	{
		refresh(player, player_ped, vehicle, shared);

		auto vehiclePos = ENTITY::GET_ENTITY_COORDS(vehicle, FALSE);
		//BOOST_LOG_TRIVIAL(info) << "sciprt main: vehicle pos: " << vehiclePos.x << " " << vehiclePos.y << " " << vehiclePos.z;

		if ((vehiclePos.x != 0) && (vehiclePos.y != 0) && (vehiclePos.z != 0))
		{
			break;
		}
		WAIT(100);
	}


	// Use NVIDIA's JSON scenario and reward definition language
	s_scenarioManager.load("intersection1.json");
	//s_scenarioManager.load("C:\\Workspace\\universe\-windows\-envs\\vnc\-gtav\\GTAVScriptHookProxy\\scenarios\\intersection1.json");
	BOOST_LOG_TRIVIAL(info) << "sciprt main: done loading scenario";

	const char s_name[] = "intersection";
	shared->scenario_name = s_name;
	s_scenarioManager.run(ScriptHookSharedMemory::shared()->scenario_name);

	std::ifstream file{ "pathnodes.txt" };
	boost::archive::text_iarchive ia{ file };
	ia >> loaded_nodes;
	BOOST_LOG_TRIVIAL(info) << "loaded path nodes";

	while (true)
	{
		Vector3 currentPosition = ENTITY::GET_ENTITY_COORDS(vehicle, FALSE);
		//status_text = "X: " + std::to_string(currentPosition.x) + " Y: " + std::to_string(currentPosition.y) + " Z: " + std::to_string(currentPosition.z);
		//update_status_text();
		//if (nodes.size() == 0)
		//{
		//	// nodes.cpp
		//	if (PATHFIND::LOAD_ALL_PATH_NODES(TRUE))
		//	{
		//		populateNodes("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Grand Theft Auto V\\paths.xml");
		//		BOOST_LOG_TRIVIAL(info) << "done populating";
		//		BOOST_LOG_TRIVIAL(info) << "node map size " << nodes.size();
		//		std::ofstream file{ "archive.txt" };
		//		boost::archive::text_oarchive oa{ file };
		//		oa << nodes;
		//		PATHFIND::LOAD_ALL_PATH_NODES(FALSE);
		//	}
		//}

		if (iter % 100 == 0) // Do this every second as the game seems to unset certain mod settings every once in a while
		{
			refresh(player, player_ped, vehicle, shared);
		}

		// use a hotkey to reset the scenario/restart episode
		// original way of restarting is to load game again so it would send keys to reload game
		// considered using 'shared->should_reset_agent', but GTAEnv is calling reset every iteration
		if (isKeyJustUp(VK_F9))
		{
			s_scenarioManager.reset();
		}
		else
		{
			// run scenario specific logic
			s_scenarioManager.onGameLoop();
		}

		// receive reset signal from universe client?
		if (shared->should_reset_agent || isKeyJustUp(VK_F11))
		{
			BOOST_LOG_TRIVIAL(info) << "Resetting agent";
			reset_agent(shared);
			shared->should_reset_agent = false;
		}

		// send game info to environment interface
		set_reward_and_info_shared_mem(shared, player, vehicle);


		const std::vector<int>& scene_actor_ids = s_scenarioManager.currentScenario().actorIDs();
		set_target_info_shared_mem(shared, scene_actor_ids, vehicle);

		set_scan_info(shared, vehicle);

		if (shared->use_agent_actions)
		{
			VEHICLE::SET_VEHICLE_FORWARD_SPEED(vehicle, shared->action_set_forward_vel);
		}

		set_scenario_dest_shared_mem(shared, s_scenarioManager.currentScenario().playerActor()->destination());

		WAIT(10);
		// set hood cam
		//CAM::SET_FOLLOW_PED_CAM_VIEW_MODE(4);
		//CAM::SET_FOLLOW_VEHICLE_CAM_VIEW_MODE(4);
		//CAM::SET_GAMEPLAY_CAM_RELATIVE_HEADING(0);
		//update_status_text();
		iter++;
	}
#else
	deep_drive();
#endif
}
