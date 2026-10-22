#pragma once
#include <vector>
#include <functional>
#include "Note.h"

// ============================================================
// PatternTool : 譜面づくりを速くする道具（エディター用）
//   ・Stamp : トリル・階段・連打などの決まった形を、BPMのグリッドにそろえて一気に置く
//   ・Copy / Paste : 何拍分かをコピーして、別の場所に貼る（左右反転もできる）
//   ・MakeNote : 1個のノーツを「レーンと時刻」だけで作る（出現位置・判定位置は自動）
//   Game のメンバーには触らない。必要な値は Context で渡す。
// ============================================================
namespace PatternTool
{
	enum class Kind
	{
		Trill,			// A B A B …
		StairsUp,		// A → B へ1レーンずつ（繰り返し）
		StairsDown,		// B → A へ1レーンずつ
		Zigzag,			// A → B → A と往復
		Jack,			// A の連打
		HandsAlternate,	// 左手(S D F)と右手(J K L)を交互。各手の中は階段
		Chord,			// A と B を同時押し
		LongTrill,		// A を押しっぱなしにしながら B を叩く
		Count
	};
	const char* KindName(Kind k);

	struct Context
	{
		float bpm = 120.0f;
		float offset = 0.0f;		// 最初の拍の位置（秒）
		std::function<DirectX::SimpleMath::Vector3(int lane, float time)> judgePosAt;	// 判定円の位置
		std::function<float(int lane)> spawnX;	// 出現位置の X
		DirectX::SimpleMath::Vector3 flickSpawn{};	// フリックの出現位置（敵の位置）
		float noteY = 0.0f;
		float spawnZ = 1000.0f;
		float travelSec = 0.5f;		// 出現→到達の秒数（エディターの3D表示用。ゲームはハイウェイ用に計算し直す）

		float SecPerBeat() const { return (bpm > 0.0f) ? 60.0f / bpm : 0.5f; }
		float BeatToTime(float beat) const { return offset + beat * SecPerBeat(); }
		float TimeToBeat(float t) const { return (t - offset) / SecPerBeat(); }
	};

	struct Params
	{
		Kind  kind = Kind::Trill;
		int   laneA = 0;			// トリル・連打・ロング：1つ目のレーン ／ 階段：始まり
		int   laneB = 1;			// トリル・同時押し：2つ目のレーン ／ 階段：終わり
		float stepBeats = 0.5f;		// ノーツの間隔（拍）。0.5 = 8分、0.25 = 16分
		float lengthBeats = 2.0f;	// 長さ（拍）
		bool  mirror = false;		// 左右反転（S と L を入れ替え）
	};

	// ロングの頭（届く時刻）。タップ・フリックは time
	inline float HeadTime(const Note& n) { return (n.note == LONGNOTE) ? n.arrivalTime : n.time; }

	// 同じレーン・ほぼ同じ時刻にノーツがあるか
	bool Exists(const std::vector<Note>& notes, int lane, float time);

	// レーンと時刻だけで1個作る（holdSec はロングの長さ。それ以外は無視）
	Note MakeNote(NoteType type, int lane, float time, float holdSec, int flickLaneTo, const Context& c);

	// startTime から置く。置いた数を返す（同じ所に既にあるノーツは飛ばす）
	int Stamp(std::vector<Note>& notes, const Params& p, float startTime, const Context& c);

	// [t0, t1) に頭があるノーツをコピー
	std::vector<Note> Copy(const std::vector<Note>& notes, float t0, float t1);

	// clip（clipT0 から始まる）を destT0 から貼る。貼った数を返す
	int Paste(std::vector<Note>& notes, const std::vector<Note>& clip, float clipT0, float destT0, bool mirror, const Context& c);

	void SortNotes(std::vector<Note>& notes);
}
