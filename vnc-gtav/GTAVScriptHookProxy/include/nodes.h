#pragma once

#include "script.h"
#include <vector>
#include <map>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

//Between ( ) default values

typedef struct {
	bool disabled; //Disabled (0)
	bool water; //Water (0)
	int speed; //Speed (4)
	int special; //Special (0)
	int density; //Density (-1)
	bool highway; //Highway (0)
	bool noGPS; //NoGps (0)
	bool tunnel; //Tunnel (0)
	bool cantGoLeft; //Cannot Go Left (0)
	bool leftTurnsOnly; //Left Turns Only (0)
	bool offRoad; //Off Road (0)
	bool cantGoRight; //Cannot Go Right (0)
	bool noBigVehicles; //No Big Vehicles (0)
	bool indicateKeepLeft; //Indicate Keep Left (0)
	bool indicateKeepRight; //Indicate Keep Right (0) 
	bool slipLane; //Slip Lane (0)

	template <typename Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & disabled;
		ar & water;
		ar & speed;
		ar & special; //Special (0)
		ar & density; //Density (-1)
		ar & highway; //Highway (0)
		ar & noGPS; //NoGps (0)
		ar & tunnel; //Tunnel (0)
		ar & cantGoLeft; //Cannot Go Left (0)
		ar & leftTurnsOnly; //Left Turns Only (0)
		ar & offRoad; //Off Road (0)
		ar & cantGoRight; //Cannot Go Right (0)
		ar & noBigVehicles; //No Big Vehicles (0)
		ar & indicateKeepLeft; //Indicate Keep Left (0)
		ar & indicateKeepRight; //Indicate Keep Right (0) 
		ar & slipLane; //Slip Lane (0)
	}
} tNodeAttr;

typedef struct {
	int width; //Width (0)
	int lanesIn; //Lanes In (1) 
	int lanesOut; //Lanes Out (1) 
	bool narrowRoad; //Narrowroad (0)
	bool gpsBothWays; //GpsBothWays (0)
	bool blockIfNoLanes; //Block If No Lanes (0)
	bool shortcut; //Shortcut (0)
	bool dontUseForNavigation; //Dont Use For Navigation (0)

	template <typename Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & width; //Width (0)
		ar & lanesIn; //Lanes In (1) 
		ar & lanesOut; //Lanes Out (1) 
		ar & narrowRoad; //Narrowroad (0)
		ar & gpsBothWays; //GpsBothWays (0)
		ar & blockIfNoLanes; //Block If No Lanes (0)
		ar & shortcut; //Shortcut (0)
		ar & dontUseForNavigation; //Dont Use For Navigation (0)
	}
} tLinkAttr;

template <typename Archive>
void serialize(Archive &ar, Vector3 &vec, const unsigned int version)
{
	ar & vec.x;
	ar & vec.y;
	ar & vec.z;
}


typedef struct {
	Vector3 coord;
	bool laneIn;

	template <typename Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & coord;
		ar & laneIn;
	}
} tLinePoint;

typedef struct{
	Vector3 coord;
	Vector3 direction;
	tLinkAttr attr;
	std::vector<tLinePoint> linePoints;

	std::string _ref1;
	std::string _ref2;

	template <typename Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & coord;
		ar & direction;
		ar & attr;
		ar & linePoints;
		ar & _ref1;
		ar & _ref2;
	}
} tLink;

typedef struct{
	int id;
	Vector3 coord;
	tNodeAttr attr;
	std::vector<tLink> links;

	template <typename Archive>
	void serialize(Archive& ar, const unsigned int version)
	{
		ar & id;
		ar & coord;
		ar & attr;
		ar & links;
	}
} tNode;

typedef struct{
	Vector3 coord1;
	Vector3 coord2;
	Vector3 direction;
} tLinkDir;

typedef struct{
	Vector3 coord1;
	Vector3 coord2;
} tLine;

extern std::unordered_map<int, tNode> nodes;

void populateNodes(const char* pathsfile);
void setLinePoints(tNode* node, tLink link);
