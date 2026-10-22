#include "pch.h"
#include "PatternTool.h"
#include <algorithm>
#include <cmath>
#undef min
#undef max

using DirectX::SimpleMath::Vector3;

namespace
{
	constexpr float kSameTimeEps = 0.005f;	// これより近い時刻は「同じ時刻」とみなす（秒）
	constexpr float kLaneCenterX = -35.0f;	// レーン0(190)とレーン5(-260)の真ん中。反転の時に使う

	bool IsFlick(NoteType t) { return t == FLICK_RIGHT || t == FLICK_LEFT; }
	int  Mirror(int lane) { return 5 - lane; }

	// A から B までのレーンの並び（A > B なら逆向き）
	std::vector<int> LaneRange(int a, int b)
	{
		std::vector<int> out;
		int step = (a <= b) ? 1 : -1;
		for (int l = a; ; l += step)
		{
			out.push_back(l);
			if (l == b) break;
		}
		return out;
	}
}

namespace PatternTool
{
	const char* KindName(Kind k)
	{
		switch (k)
		{
		case Kind::Trill:          return "Trill (A B A B)";
		case Kind::StairsUp:       return "Stairs A->B";
		case Kind::StairsDown:     return "Stairs B->A";
		case Kind::Zigzag:         return "Zigzag A<->B";
		case Kind::Jack:           return "Jack (A A A)";
		case Kind::HandsAlternate: return "Hands L/R";
		case Kind::Chord:          return "Chord (A+B)";
		case Kind::LongTrill:      return "Long A + tap B";
		default:                   return "?";
		}
	}

	bool Exists(const std::vector<Note>& notes, int lane, float time)
	{
		for (const auto& n : notes)
			if (n.lane == lane && std::fabs(HeadTime(n) - time) < kSameTimeEps) return true;
		return false;
	}

	Note MakeNote(NoteType type, int lane, float time, float holdSec, int flickLaneTo, const Context& c)
	{
		Note n = Note();
		n.note = type;
		n.lane = lane;
		n.flickLaneTo = IsFlick(type) ? flickLaneTo : -1;
		n.arrivalTime = time;
		if (type == LONGNOTE)
		{
			n.holdDuration = holdSec;
			n.time = time + holdSec;
		}
		else
		{
			n.holdDuration = 0.0f;
			n.time = time;
		}
		n.spawnTime = std::max(0.0f, time - c.travelSec);

		if (IsFlick(type))
		{
			n.spawnPos = Vector3(c.flickSpawn.x, c.noteY, c.flickSpawn.z);	// 手で置く時と同じ（敵の位置から）
			n.judgePos = Vector3(0.0f, c.noteY, -210.0f);
		}
		else
		{
			n.spawnPos = Vector3(c.spawnX ? c.spawnX(lane) : 0.0f, c.noteY, c.spawnZ);
			n.judgePos = c.judgePosAt ? c.judgePosAt(lane, time) : Vector3::Zero;
		}
		return n;
	}

	int Stamp(std::vector<Note>& notes, const Params& p, float startTime, const Context& c)
	{
		const float spb = c.SecPerBeat();
		const float stepSec = std::max(0.01f, p.stepBeats) * spb;
		const int count = std::max(1, (int)std::lround(p.lengthBeats / std::max(0.01f, p.stepBeats)));

		const int a = std::clamp(p.laneA, 0, 5);
		const int b = std::clamp(p.laneB, 0, 5);
		auto fix = [&](int lane) { return p.mirror ? Mirror(lane) : lane; };

		int placed = 0;
		auto put = [&](NoteType type, int lane, float t, float hold)
			{
				lane = fix(lane);
				if (Exists(notes, lane, t)) return;
				notes.push_back(MakeNote(type, lane, t, hold, -1, c));
				placed++;
			};

		std::vector<int> range = LaneRange(a, b);
		std::vector<int> zig = range;					// 往復：A..B..(Aの手前)
		for (int i = (int)range.size() - 2; i >= 1; i--) zig.push_back(range[i]);
		const int left[3] = { 0, 1, 2 };
		const int right[3] = { 3, 4, 5 };

		if (p.kind == Kind::LongTrill)
		{
			put(LONGNOTE, a, startTime, count * stepSec);	// A は最初から最後まで押しっぱなし
		}

		for (int i = 0; i < count; i++)
		{
			float t = startTime + i * stepSec;
			switch (p.kind)
			{
			case Kind::Trill:          put(TAP, (i % 2 == 0) ? a : b, t, 0.0f); break;
			case Kind::StairsUp:       put(TAP, range[i % range.size()], t, 0.0f); break;
			case Kind::StairsDown:     put(TAP, range[range.size() - 1 - (i % range.size())], t, 0.0f); break;
			case Kind::Zigzag:         put(TAP, zig[i % zig.size()], t, 0.0f); break;
			case Kind::Jack:           put(TAP, a, t, 0.0f); break;
			case Kind::HandsAlternate: put(TAP, (i % 2 == 0) ? left[(i / 2) % 3] : right[(i / 2) % 3], t, 0.0f); break;
			case Kind::Chord:          put(TAP, a, t, 0.0f); put(TAP, b, t, 0.0f); break;
			case Kind::LongTrill:      put(TAP, b, t, 0.0f); break;
			default: break;
			}
		}
		SortNotes(notes);
		return placed;
	}

	std::vector<Note> Copy(const std::vector<Note>& notes, float t0, float t1)
	{
		std::vector<Note> out;
		for (const auto& n : notes)
		{
			float h = HeadTime(n);
			if (h >= t0 - kSameTimeEps && h < t1 - kSameTimeEps) out.push_back(n);
		}
		return out;
	}

	int Paste(std::vector<Note>& notes, const std::vector<Note>& clip, float clipT0, float destT0, bool mirror, const Context& c)
	{
		const float dt = destT0 - clipT0;
		int placed = 0;
		for (Note n : clip)
		{
			n.time += dt;
			n.arrivalTime += dt;
			n.spawnTime = std::max(0.0f, n.spawnTime + dt);
			for (auto& tr : n.turns) tr.time += dt;

			if (mirror)
			{
				n.lane = Mirror(n.lane);
				if (n.flickLaneTo >= 0) n.flickLaneTo = Mirror(n.flickLaneTo);
				if (n.note == FLICK_RIGHT) n.note = FLICK_LEFT;
				else if (n.note == FLICK_LEFT) n.note = FLICK_RIGHT;
				for (auto& tr : n.turns) tr.lane = Mirror(tr.lane);
				for (auto& cp : n.controlPoints) cp.x = 2.0f * kLaneCenterX - cp.x;
			}

			float head = HeadTime(n);
			if (Exists(notes, n.lane, head)) continue;

			if (!IsFlick(n.note))
			{
				if (c.spawnX) n.spawnPos.x = c.spawnX(n.lane);
				if (c.judgePosAt) n.judgePos = c.judgePosAt(n.lane, head);
			}
			notes.push_back(n);
			placed++;
		}
		SortNotes(notes);
		return placed;
	}

	void SortNotes(std::vector<Note>& notes)
	{
		std::stable_sort(notes.begin(), notes.end(),
			[](const Note& x, const Note& y) { return x.time < y.time; });	// エディターの今の並べ方に合わせる
	}
}
