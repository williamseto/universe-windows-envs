
#pragma once

#include <vector>
#include <memory>
#include "IActor.h"
#include <types.h>
#include <natives.h>

namespace Nvidia
{
	class ObjectActor : public IActor
	{
	public:
		ObjectActor(const std::string & objectName, const Vector3 & pos, const Vector3 & rot) :
			m_pos(pos), m_rot(rot)
		{
			// request game to load model into memory so we can spawn it later
			m_hash = GAMEPLAY::GET_HASH_KEY(const_cast<char*>(objectName.c_str()));
			STREAMING::REQUEST_MODEL(m_hash);
		}

		virtual void addTask(std::unique_ptr<ITask> task) override {}

		virtual void setupActor(DWORD currentTick) override
		{
			while (!STREAMING::HAS_MODEL_LOADED(m_hash)) WAIT(0);
			m_objHandle = OBJECT::CREATE_OBJECT(m_hash, m_pos.x, m_pos.y, m_pos.z, 1, 1, 0);
			ENTITY::SET_ENTITY_ROTATION(m_objHandle, m_rot.x, m_rot.y, m_rot.z, 2, 1);
		}

		virtual void onTick(DWORD currentTick) override {}

		virtual void cleanUpActor() override {}

		virtual bool isEntityOurActor(Entity e) const override { return false; }

		virtual int id() override { return m_objHandle; }

	protected:

		Vector3 m_pos;
		Vector3 m_rot;

		int m_hash;
		Object m_objHandle;
		

	};
}

