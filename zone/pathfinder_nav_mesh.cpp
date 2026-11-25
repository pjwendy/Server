/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
// dtNavMesh::connectExtOffMeshLinks is private: this file reaches it to join long off-mesh links (ConnectFarOffMeshLinks, below). The class's
// layout is the same either way; only this translation unit sees it public
#define private public
#include "recastnavigation/DetourNavMesh.h"
#undef private
#include "pathfinder_nav_mesh.h"
#include <set>
#include <tuple>

namespace {
	constexpr unsigned char AVOID_AREA = 12;     // an area no navmesh has: the polygons a route keeps out of
	constexpr float         AVOID_COST = 20.0f;  // ... each unit through one costs this many round
	constexpr float         AVOID_DZ   = 40.0f;  // ... within this of the mob's height
	constexpr float         HEADLESS_LAVA_COST = 50.0f; // lava (navbuild marks the headless navmesh's): next to never
}

#include "common/compression.h"
#include "zone/client.h"
#include "zone/water_map.h"
#include "zone/zone.h"

#include "recastnavigation/DetourCommon.h"
#include "recastnavigation/DetourNavMeshQuery.h"
#include <cstdio>
#include <memory>
#include <vector>

extern Zone *zone;

struct PathfinderNavmesh::Implementation
{
	dtNavMesh *nav_mesh;
	dtNavMeshQuery *query;
};

PathfinderNavmesh::PathfinderNavmesh(const std::string &path)
{
	m_impl = std::make_unique<Implementation>();
	m_impl->nav_mesh = nullptr;
	m_impl->query = nullptr;
	Load(path);
}

PathfinderNavmesh::~PathfinderNavmesh()
{
	Clear();
}

bool PathfinderNavmesh::NavHeight(const glm::vec3 &at, float dz, float &z, int flags)
{
	if (!m_impl->nav_mesh) {
		return false;
	}
	if (!m_impl->query) {
		m_impl->query = dtAllocNavMeshQuery();
		m_impl->query->init(m_impl->nav_mesh, RuleI(Pathing, MaxNavmeshNodes));
	}
	dtQueryFilter filter;
	filter.setIncludeFlags(flags);
	filter.setExcludeFlags((flags & PathingHeadless) ? 0 : PathingHeadless);
	const glm::vec3 pos(at.x, at.z, at.y); // (Detour's y is up)
	const glm::vec3 ext(1.0f, dz, 1.0f);
	dtPolyRef ref = 0;
	glm::vec3 nearest(0.0f);
	if (dtStatusFailed(m_impl->query->findNearestPoly(&pos[0], &ext[0], &filter, &ref, &nearest[0])) || !ref) {
		return false;
	}
	float h = 0.0f;
	if (dtStatusFailed(m_impl->query->getPolyHeight(ref, &pos[0], &h))) {
		return false;
	}
	z = h;
	return true;
}

bool PathfinderNavmesh::NavNearest(const glm::vec3 &at, const glm::vec3 &half, glm::vec3 &out, int flags)
{
	if (!m_impl->nav_mesh) {
		return false;
	}
	if (!m_impl->query) {
		m_impl->query = dtAllocNavMeshQuery();
		m_impl->query->init(m_impl->nav_mesh, RuleI(Pathing, MaxNavmeshNodes));
	}
	dtQueryFilter filter;
	filter.setIncludeFlags(flags);
	filter.setExcludeFlags((flags & PathingHeadless) ? 0 : PathingHeadless);
	const glm::vec3 pos(at.x, at.z, at.y); // (Detour's y is up)
	const glm::vec3 ext(half.x, half.z, half.y);
	dtPolyRef ref = 0;
	glm::vec3 nearest(0.0f);
	if (dtStatusFailed(m_impl->query->findNearestPoly(&pos[0], &ext[0], &filter, &ref, &nearest[0])) || !ref) {
		return false;
	}
	out = glm::vec3(nearest.x, nearest.z, nearest.y);
	return true;
}

IPathfinder::IPath PathfinderNavmesh::FindRouteAvoiding(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck,
	int flags, const std::vector<glm::vec4> &avoid)
{
	if (!m_impl->nav_mesh || avoid.empty()) {
		return FindRoute(start, end, partial, stuck, flags);
	}
	if (!m_impl->query) {
		m_impl->query = dtAllocNavMeshQuery();
		m_impl->query->init(m_impl->nav_mesh, RuleI(Pathing, MaxNavmeshNodes));
	}
	// the polygons within each circle take the avoided area for this one route (the zone's one thread:
	// nothing else plans in between), at AVOID_COST to cross, then have their own areas back. A
	// circle round our start or our destination isn't avoided: there's no way round it
	dtQueryFilter all;
	all.setIncludeFlags(PathingAll);
	std::vector<std::pair<dtPolyRef, unsigned char>> marked;
	dtPolyRef polys[256];
	for (const auto &c : avoid) {
		const glm::vec2 at(c.x, c.y);
		if (glm::length(at - glm::vec2(start.x, start.y)) <= c.w || glm::length(at - glm::vec2(end.x, end.y)) <= c.w) {
			continue;
		}
		const float centre[3] = {c.x, c.z, c.y}, ext[3] = {c.w, AVOID_DZ, c.w}; // (Detour's y is up)
		int n = 0;
		if (dtStatusFailed(m_impl->query->queryPolygons(centre, ext, &all, polys, &n, 256))) {
			continue;
		}
		for (int i = 0; i < n; ++i) {
			float nearest[3];
			bool  over = false;
			unsigned char area = 0;
			if (dtStatusFailed(m_impl->nav_mesh->getPolyArea(polys[i], &area)) || area == AVOID_AREA ||
				dtStatusFailed(m_impl->query->closestPointOnPoly(polys[i], centre, nearest, &over)) ||
				glm::length(glm::vec2(nearest[0] - c.x, nearest[2] - c.y)) > c.w) {
				continue;
			}
			marked.emplace_back(polys[i], area);
			m_impl->nav_mesh->setPolyArea(polys[i], AVOID_AREA);
		}
	}
	auto route = FindRoute(start, end, partial, stuck, flags);
	for (auto it = marked.rbegin(); it != marked.rend(); ++it) {
		m_impl->nav_mesh->setPolyArea(it->first, it->second);
	}
	return route;
}

IPathfinder::IPath PathfinderNavmesh::FindRoute(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck, int flags)
{
	partial = false;

	if (!m_impl->nav_mesh) {
		return IPath();
	}

	if (!m_impl->query) {
		m_impl->query = dtAllocNavMeshQuery();
	}

	// (a headless character's route searches more: Jaggedpine's player navmesh, fine-grained, ran 16384 nodes out on a 5,000-unit walk, and
	// the search's best was the tree platform straight over Lady Fianya's spawn, 180 up: never there, 11 Nov. NPCs keep the rule's)
	constexpr int HEADLESS_NODES = 65535;
	m_impl->query->init(m_impl->nav_mesh, (flags & PathingHeadless) ? std::max(RuleI(Pathing, MaxNavmeshNodes), HEADLESS_NODES)
		: RuleI(Pathing, MaxNavmeshNodes));
	glm::vec3 current_location(start.x, start.z, start.y);
	glm::vec3 dest_location(end.x, end.z, end.y);

	dtQueryFilter filter;
	filter.setIncludeFlags(flags);
	// (a headless link also carries Normal or Water; and a headless route never goes through lava: our characters take no lava damage
	// (the client reports it, OP_EnvDamage, and the headless mover reports drowning only), so a lava wade was a cheat, leaders crossing
	// under Nagafen's raised drawbridge at full health; the operator, 25 Nov: avoid it until lava damage is done, BL-110)
	filter.setExcludeFlags((flags & PathingHeadless) ? PathingLava : PathingHeadless);
	filter.setAreaCost(0, 1.0f); //Normal
	filter.setAreaCost(1, 3.0f); //Water
	filter.setAreaCost(2, (flags & PathingHeadless) ? HEADLESS_LAVA_COST : 5.0f); //Lava (a headless character never takes it lightly)
	filter.setAreaCost(4, 1.0f); //PvP
	filter.setAreaCost(5, 2.0f); //Slime
	filter.setAreaCost(6, 2.0f); //Ice
	filter.setAreaCost(7, 4.0f); //V Water (Frigid Water)
	filter.setAreaCost(8, 1.0f); //General Area
	filter.setAreaCost(9, 0.1f); //Portal
	filter.setAreaCost(10, 0.1f); //Prefer
	filter.setAreaCost(AVOID_AREA, AVOID_COST); // (FindRouteAvoiding's, for the length of one route)

	dtPolyRef start_ref;
	dtPolyRef end_ref;
	glm::vec3 ext(5.0f, 100.0f, 5.0f);

	// (the headless characters' routes come here, not FindPath: the same snap to a floor at the point's own level, also when the narrow box
	// finds no polygon at all. Jaggedpine's Lady Fianya: the spawn point is more than 5 from the ground's polygons, and the route went to
	// the platform 180 over it, after the fix went into FindPath only, 11 Nov)
	constexpr float LEVEL_DZ    = 25.0f;
	constexpr float LEVEL_REACH = 40.0f;
	auto snap = [&](const glm::vec3 &at, dtPolyRef &ref) {
		glm::vec3 got = at;
		ref = 0;
		m_impl->query->findNearestPoly(&at[0], &ext[0], &filter, &ref, &got[0]);
		if (ref && std::fabs(got.y - at.y) <= LEVEL_DZ) {
			return;
		}
		const glm::vec3 level(LEVEL_REACH, LEVEL_DZ, LEVEL_REACH);
		dtPolyRef  same = 0;
		glm::vec3  there = at;
		if (dtStatusSucceed(m_impl->query->findNearestPoly(&at[0], &level[0], &filter, &same, &there[0])) && same) {
			ref = same;
		}
	};
	snap(current_location, start_ref);
	snap(dest_location, end_ref);

	if (!start_ref || !end_ref) {
		return IPath();
	}

	int npoly = 0;
	dtPolyRef path[1024] = { 0 };
	auto status = m_impl->query->findPath(start_ref, end_ref, &current_location[0], &dest_location[0], &filter, path, &npoly, 1024);

	if (npoly) {
		glm::vec3 epos = dest_location;
		if (path[npoly - 1] != end_ref) {
			m_impl->query->closestPointOnPoly(path[npoly - 1], &dest_location[0], &epos[0], 0);
			partial = true;

			auto dist = DistanceSquared(epos, current_location);
			if (dist < 10000.0f) {
				stuck = true;
			}
		}

		float straight_path[2048 * 3];
		unsigned char straight_path_flags[2048];

		int n_straight_polys;
		dtPolyRef straight_path_polys[2048];

		status = m_impl->query->findStraightPath(&current_location[0], &epos[0], path, npoly,
			straight_path, straight_path_flags,
			straight_path_polys, &n_straight_polys, 2048, DT_STRAIGHTPATH_AREA_CROSSINGS);

		if (dtStatusFailed(status)) {
			return IPath();
		}

		if (n_straight_polys) {
			IPath Route;
			for (int i = 0; i < n_straight_polys; ++i)
			{
				glm::vec3 node;
				node.x = straight_path[i * 3];
				node.z = straight_path[i * 3 + 1];
				node.y = straight_path[i * 3 + 2];

				Route.push_back(node);

				unsigned short flag = 0;
				if (dtStatusSucceed(m_impl->nav_mesh->getPolyFlags(straight_path_polys[i], &flag))) {
					if (flag & 512) {
						Route.push_back(true);
					}
				}
			}

			return Route;
		}
	}

	IPath Route;
	Route.push_back(end);
	return Route;
}

IPathfinder::IPath PathfinderNavmesh::FindPath(const glm::vec3 &start, const glm::vec3 &end, bool &partial, bool &stuck, const PathfinderOptions &opts)
{
	partial = false;

	if (!m_impl->nav_mesh) {
		return IPath();
	}

	if (!m_impl->query) {
		m_impl->query = dtAllocNavMeshQuery();
	}

	m_impl->query->init(m_impl->nav_mesh, RuleI(Pathing, MaxNavmeshNodes));
	glm::vec3 current_location(start.x, start.z, start.y);
	glm::vec3 dest_location(end.x, end.z, end.y);

	dtQueryFilter filter;
	filter.setIncludeFlags(opts.flags);
	filter.setExcludeFlags((opts.flags & PathingHeadless) ? 0 : PathingHeadless);
	filter.setAreaCost(0, opts.flag_cost[0]); //Normal
	filter.setAreaCost(1, opts.flag_cost[1]); //Water
	filter.setAreaCost(2, (opts.flags & PathingHeadless) ? HEADLESS_LAVA_COST : opts.flag_cost[2]); //Lava (a bot on its owner's navmesh shies from it as the owner does)
	filter.setAreaCost(4, opts.flag_cost[3]); //PvP
	filter.setAreaCost(5, opts.flag_cost[4]); //Slime
	filter.setAreaCost(6, opts.flag_cost[5]); //Ice
	filter.setAreaCost(7, opts.flag_cost[6]); //V Water (Frigid Water)
	filter.setAreaCost(8, opts.flag_cost[7]); //General Area
	filter.setAreaCost(9, opts.flag_cost[8]); //Portal
	filter.setAreaCost(10, opts.flag_cost[9]); //Prefer

	static const int max_polys = 256;
	dtPolyRef start_ref;
	dtPolyRef end_ref;
	glm::vec3 ext(10.0f, 200.0f, 10.0f);

	// the nearest polygon in that tall, narrow box can be a floor far over or under the point: a tree's platform 200 over a spawn point
	// beside its trunk, where the ground's polygons start more than 10 out (Jaggedpine's Lady Fianya: every route "the nearest it gets is"
	// the platform, 11 Nov). Then a polygon at the point's own level, a little further out, is the floor meant
	constexpr float LEVEL_DZ    = 25.0f;
	constexpr float LEVEL_REACH = 40.0f;
	auto snap = [&](const glm::vec3 &at, dtPolyRef &ref) {
		glm::vec3 got = at;
		ref = 0;
		m_impl->query->findNearestPoly(&at[0], &ext[0], &filter, &ref, &got[0]);
		if (!ref || std::fabs(got.y - at.y) <= LEVEL_DZ) {
			return;
		}
		const glm::vec3 level(LEVEL_REACH, LEVEL_DZ, LEVEL_REACH);
		dtPolyRef  same = 0;
		glm::vec3  there = at;
		if (dtStatusSucceed(m_impl->query->findNearestPoly(&at[0], &level[0], &filter, &same, &there[0])) && same) {
			ref = same;
		}
	};
	snap(current_location, start_ref);
	snap(dest_location, end_ref);

	if (!start_ref || !end_ref) {
		return IPath();
	}

	int npoly = 0;
	dtPolyRef path[max_polys] = { 0 };
	auto status = m_impl->query->findPath(start_ref, end_ref, &current_location[0], &dest_location[0], &filter, path, &npoly, max_polys);

	if (npoly) {
		glm::vec3 epos = dest_location;
		if (path[npoly - 1] != end_ref) {
			m_impl->query->closestPointOnPoly(path[npoly - 1], &dest_location[0], &epos[0], 0);
			partial = true;

			auto dist = DistanceSquared(epos, current_location);
			if (dist < 10000.0f) {
				stuck = true;
			}
		}

		int n_straight_polys;
		glm::vec3 straight_path[max_polys];
		unsigned char straight_path_flags[max_polys];
		dtPolyRef straight_path_polys[max_polys];

		auto status = m_impl->query->findStraightPath(&current_location[0], &epos[0], path, npoly,
			(float*)&straight_path[0], straight_path_flags,
			straight_path_polys, &n_straight_polys, 2048, DT_STRAIGHTPATH_AREA_CROSSINGS | DT_STRAIGHTPATH_ALL_CROSSINGS);

		if (dtStatusFailed(status)) {
			return IPath();
		}

		if (n_straight_polys) {
			if (opts.smooth_path) {
				IPath Route;

				//Add the first point
				{
					auto &flag = straight_path_flags[0];
					if (flag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) {
						auto &p = straight_path[0];

						Route.push_back(glm::vec3(p.x, p.z, p.y));
					}
					else {
						auto &p = straight_path[0];

						float h = 0.0f;
						if (dtStatusSucceed(GetPolyHeightOnPath(path, npoly, p, &h))) {
							p.y = h + opts.offset;
						}

						Route.push_back(glm::vec3(p.x, p.z, p.y));
					}
				}

				for (int i = 0; i < n_straight_polys - 1; ++i)
				{
					auto &flag = straight_path_flags[i];

					if (flag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) {
						auto &poly = straight_path_polys[i];

						auto &p2 = straight_path[i + 1];
						glm::vec3 node(p2.x, p2.z, p2.y);
						Route.push_back(node);

						unsigned short pflag = 0;
						if (dtStatusSucceed(m_impl->nav_mesh->getPolyFlags(straight_path_polys[i], &pflag))) {
							if (pflag & 512) {
								Route.push_back(true);
							}
						}
					}
					else {
						auto &p1 = straight_path[i];
						auto &p2 = straight_path[i + 1];
						auto dist = glm::distance(p1, p2);
						auto dir = glm::normalize(p2 - p1);
						float total = 0.0f;
						glm::vec3 previous_pt = p1;

						while (total < dist) {
							glm::vec3 current_pt;
							float dist_to_move = opts.step_size;
							float ff = opts.step_size / 2.0f;

							if (total + dist_to_move + ff >= dist) {
								current_pt = p2;
								total = dist;
							}
							else {
								total += dist_to_move;
								current_pt = p1 + dir * total;
							}

							float h = 0.0f;
							if (dtStatusSucceed(GetPolyHeightOnPath(path, npoly, current_pt, &h))) {
								current_pt.y = h + opts.offset;
							}

							Route.push_back(glm::vec3(current_pt.x, current_pt.z, current_pt.y));
							previous_pt = current_pt;
						}
					}
				}

				return Route;
			}
			else {
				IPath Route;
				for (int i = 0; i < n_straight_polys; ++i)
				{
					auto &current = straight_path[i];
					glm::vec3 node(current.x, current.z, current.y);
					Route.push_back(node);

					unsigned short flag = 0;
					if (dtStatusSucceed(m_impl->nav_mesh->getPolyFlags(straight_path_polys[i], &flag))) {
						if (flag & 512) {
							Route.push_back(true);
						}
					}
				}

				return Route;
			}
		}
	}

	return IPath();
}

glm::vec3 PathfinderNavmesh::GetRandomLocation(const glm::vec3 &start, int flags)
{
	if (start.x == 0.0f && start.y == 0.0)
		return glm::vec3(0.f);

	if (!m_impl->nav_mesh) {
		return glm::vec3(0.f);
	}

	if (!m_impl->query) {
		m_impl->query = dtAllocNavMeshQuery();
		m_impl->query->init(m_impl->nav_mesh, RuleI(Pathing, MaxNavmeshNodes));
	}

	dtQueryFilter filter;
	filter.setIncludeFlags(flags);
	// (a headless link also carries Normal or Water; and a headless route never goes through lava: our characters take no lava damage
	// (the client reports it, OP_EnvDamage, and the headless mover reports drowning only), so a lava wade was a cheat, leaders crossing
	// under Nagafen's raised drawbridge at full health; the operator, 25 Nov: avoid it until lava damage is done, BL-110)
	filter.setExcludeFlags((flags & PathingHeadless) ? PathingLava : PathingHeadless);
	filter.setAreaCost(0, 1.0f); //Normal
	filter.setAreaCost(1, 3.0f); //Water
	filter.setAreaCost(2, (flags & PathingHeadless) ? HEADLESS_LAVA_COST : 5.0f); //Lava (a headless character never takes it lightly)
	filter.setAreaCost(4, 1.0f); //PvP
	filter.setAreaCost(5, 2.0f); //Slime
	filter.setAreaCost(6, 2.0f); //Ice
	filter.setAreaCost(7, 4.0f); //V Water (Frigid Water)
	filter.setAreaCost(8, 1.0f); //General Area
	filter.setAreaCost(9, 0.1f); //Portal
	filter.setAreaCost(10, 0.1f); //Prefer

	dtPolyRef randomRef;
	float point[3];

	dtPolyRef start_ref;
	glm::vec3 current_location(start.x, start.z, start.y);
	glm::vec3 ext(5.0f, 100.0f, 5.0f);

	m_impl->query->findNearestPoly(&current_location[0], &ext[0], &filter, &start_ref, 0);

	if (!start_ref)
	{
		return glm::vec3(0.f);
	}

	if (dtStatusSucceed(m_impl->query->findRandomPointAroundCircle(start_ref, &current_location[0], 100.f, &filter, []() { return (float)zone->random.Real(0.0, 1.0); }, &randomRef, point)))
	{
		return glm::vec3(point[0], point[2], point[1]);
	}

	return glm::vec3(0.f);
}

void PathfinderNavmesh::DebugCommand(Client *c, const Seperator *sep)
{
	if (sep->arg[1][0] == '\0' || !strcasecmp(sep->arg[1], "help"))
	{
		c->Message(Chat::White, "#path show: Plots a path from the user to their target.");
		return;
	}

	if (!strcasecmp(sep->arg[1], "show"))
	{
		if (c->GetTarget() != nullptr) {
			auto target = c->GetTarget();
			glm::vec3 start(c->GetX(), c->GetY(), c->GetZ());
			glm::vec3 end(target->GetX(), target->GetY(), target->GetZ());

			ShowPath(c, start, end);
		}

		return;
	}
}

void PathfinderNavmesh::Clear()
{
	if (m_impl->nav_mesh) {
		dtFreeNavMesh(m_impl->nav_mesh);
	}

	if (m_impl->query) {
		dtFreeNavMeshQuery(m_impl->query);
	}
}

// Detour joins an off-mesh link's far end to the navmesh only in its own tile or a neighbouring one: a longer link (a teleport pad across a city,
// 375 units in Erudin; 233 of them in eleven zones: Skyshrine's, the Plane of Thunder's, Felwithe's...) led nowhere, and the zone's parts it
// joins were cut off from each other (Serthos, 6 Nov, in Erudin's palace landing with no way back into the city). Each such link is joined
// here to the tile its far end lies in, by Detour's own routine. Returns the links looked at.
static int ConnectFarOffMeshLinks(dtNavMesh *mesh)
{
	std::set<std::tuple<const dtMeshTile *, const dtMeshTile *, int>> done;
	int joined = 0;
	for (int i = 0; i < mesh->getMaxTiles(); ++i) {
		dtMeshTile *tile = const_cast<dtMeshTile *>(static_cast<const dtNavMesh *>(mesh)->getTile(i));
		if (!tile || !tile->header) {
			continue;
		}
		for (int k = 0; k < tile->header->offMeshConCount; ++k) {
			const dtOffMeshConnection &con = tile->offMeshCons[k];
			if (con.side == 0xff) {
				continue;  // (ends in its own tile)
			}
			int tx = 0, ty = 0;
			mesh->calcTileLoc(&con.pos[3], &tx, &ty);
			if (std::abs(tx - tile->header->x) <= 1 && std::abs(ty - tile->header->y) <= 1) {
				continue;  // (a neighbour: Detour joined it)
			}
			const dtMeshTile *ends[32];
			const int n = mesh->getTilesAt(tx, ty, ends, 32);
			for (int j = 0; j < n; ++j) {
				if (done.insert({ends[j], tile, con.side}).second) {
					mesh->connectExtOffMeshLinks(const_cast<dtMeshTile *>(ends[j]), tile, dtOppositeTile(con.side));
				}
			}
			++joined;
		}
	}
	return joined;
}

void PathfinderNavmesh::Load(const std::string &path)
{
	Clear();

	FILE *f = fopen(path.c_str(), "rb");
	if (f) {
		char magic[9] = { 0 };
		if (fread(magic, 9, 1, f) != 1) {
			fclose(f);
			return;
		}

		if (strncmp(magic, "EQNAVMESH", 9) != 0)
		{
			fclose(f);
			return;
		}

		uint32_t version = 0;
		if (fread(&version, sizeof(uint32_t), 1, f) != 1) {
			fclose(f);
			return;
		}

		if (version != 2) {
			fclose(f);
			return;
		}

		uint32_t data_size;
		if (fread(&data_size, sizeof(data_size), 1, f) != 1) {
			fclose(f);
			return;
		}

		uint32_t buffer_size;
		if (fread(&buffer_size, sizeof(buffer_size), 1, f) != 1) {
			fclose(f);
			return;
		}

		std::vector<char> data;
		data.resize(data_size);
		if (fread(&data[0], data_size, 1, f) != 1) {
			fclose(f);
			return;
		}

		std::vector<char> buffer;
		buffer.resize(buffer_size);
		uint32_t v = EQ::InflateData(&data[0], data_size, &buffer[0], buffer_size);
		fclose(f);

		char *buf = &buffer[0];
		m_impl->nav_mesh = dtAllocNavMesh();

		uint32_t number_of_tiles = *(uint32_t*)buf;
		buf += sizeof(uint32_t);

		dtNavMeshParams params = *(dtNavMeshParams*)buf;
		buf += sizeof(dtNavMeshParams);

		dtStatus status = m_impl->nav_mesh->init(&params);
		if (dtStatusFailed(status))
		{
			dtFreeNavMesh(m_impl->nav_mesh);
			m_impl->nav_mesh = nullptr;
			return;
		}

		for (unsigned int i = 0; i < number_of_tiles; ++i)
		{
			uint32_t tile_ref = *(uint32_t*)buf;
			buf += sizeof(uint32_t);

			int32_t data_size = *(uint32_t*)buf;
			buf += sizeof(uint32_t);

			if (!tile_ref || !data_size) {
				dtFreeNavMesh(m_impl->nav_mesh);
				m_impl->nav_mesh = nullptr;
				return;
			}

			unsigned char* data = (unsigned char*)dtAlloc(data_size, DT_ALLOC_PERM);
			memcpy(data, buf, data_size);
			buf += data_size;

			m_impl->nav_mesh->addTile(data, data_size, DT_TILE_FREE_DATA, tile_ref, 0);
		}

		const int far_links = ConnectFarOffMeshLinks(m_impl->nav_mesh);
		LogInfo("Loaded Navmesh V[{}] file [{}] ({} long off-mesh links joined at their far end)", version, path.c_str(), far_links);
	}
}

void PathfinderNavmesh::ShowPath(Client * c, const glm::vec3 &start, const glm::vec3 &end)
{
	auto &list = entity_list.GetNPCList();

	for (auto &iter : list) {
		auto npc = iter.second;
		auto name = npc->GetName();

		if (strstr(name, "PathNode") != nullptr) {
			npc->Depop();
		}
	}

	PathfinderOptions opts;
	opts.smooth_path = true;
	opts.step_size = RuleR(Pathing, NavmeshStepSize);
	bool partial = false;
	bool stuck = false;
	auto path = FindPath(start, end, partial, stuck, opts);

	for (auto &node : path) {
		if (!node.teleport) {
			NPC::SpawnNPC("PathNode 2253 1 0 1 2 1", glm::vec4(node.pos, 1.0));
		}
	}
}

dtStatus PathfinderNavmesh::GetPolyHeightNoConnections(dtPolyRef ref, const float *pos, float *height) const
{
	auto *m_nav = m_impl->nav_mesh;

	if (!m_nav) {
		return DT_FAILURE;
	}

	const dtMeshTile* tile = 0;
	const dtPoly* poly = 0;
	if (dtStatusFailed(m_nav->getTileAndPolyByRef(ref, &tile, &poly))) {
		return DT_FAILURE | DT_INVALID_PARAM;
	}

	if (poly->getType() != DT_POLYTYPE_OFFMESH_CONNECTION) {
		const unsigned int ip = (unsigned int)(poly - tile->polys);
		const dtPolyDetail* pd = &tile->detailMeshes[ip];
		for (int j = 0; j < pd->triCount; ++j)
		{
			const unsigned char* t = &tile->detailTris[(pd->triBase + j) * 4];
			const float* v[3];
			for (int k = 0; k < 3; ++k)
			{
				if (t[k] < poly->vertCount)
					v[k] = &tile->verts[poly->verts[t[k]] * 3];
				else
					v[k] = &tile->detailVerts[(pd->vertBase + (t[k] - poly->vertCount)) * 3];
			}
			float h;
			if (dtClosestHeightPointTriangle(pos, v[0], v[1], v[2], h))
			{
				if (height)
					*height = h;
				return DT_SUCCESS;
			}
		}
	}

	return DT_FAILURE | DT_INVALID_PARAM;
}

dtStatus PathfinderNavmesh::GetPolyHeightOnPath(const dtPolyRef *path, const int path_len, const glm::vec3 &pos, float *h) const
{
	if (!path || !path_len) {
		return DT_FAILURE;
	}

	for (int i = 0; i < path_len; ++i) {
		dtPolyRef ref = path[i];

		if (dtStatusSucceed(GetPolyHeightNoConnections(ref, &pos[0], h))) {
			return DT_SUCCESS;
		}
	}

	return DT_FAILURE;
}
