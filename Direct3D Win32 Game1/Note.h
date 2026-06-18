#pragma once
#include <vector>
#include <SimpleMath.h>

enum NoteType
{
    TAP,
    LONG_START,
    LONG_CENTER,
    LONG_END,
    UNTIMIRROR, //ミラーの影響を受けないノーツ
    MIRRORNOTE, //叩いたらミラーオンオフが切り替わる
    BEAM,
    SLIDE_RIGHT,
    SLIDE_LEFT,
    SLIDE_UP,
    SLIDE_DOWN,
};

struct Note
{
public:
    int lane = 0;   // キー対応（0=S,1=D,2=F,3=J,4=K,5=L）
    float y;    // Y座標
    float time; //判定時刻
    float spawnTime = -1.0f;    //出現時刻(-1ならtime = -2.0f)
    float startLong;
    float endLong;
    float lenght;   //ビーム用
    NoteType note = TAP;


    //3D座標
    DirectX::SimpleMath::Vector3 spawnPos = { 0.0f,-55.0f,1000.0f };    //出現位置
    DirectX::SimpleMath::Vector3 judgePos = { 0.0f,-55.0f,-210.0f };    //到達位置


    std::vector<DirectX::SimpleMath::Vector3> controlPoints;

    Note();
    //Note(int lane, float t, NoteType note);
    Note(int lane, float t, NoteType note)
        : lane(lane), time(t), note(note)
    {
        // レーンXをデフォルトのspawnPos/judgePosに反映
        float laneX = -310.0f + (660.0f / 6.0f) * lane + (660.0f / 12.0f);
        spawnPos = { laneX, -55.0f,  1000.0f };
        judgePos = { laneX, -55.0f, -210.0f };
    }

    float GetTime() const { return time; }  // getter

    //スポーン時間を取得
    float GetSpawnTime() const
    {
        return (spawnTime < 0.0f) ? (time - 2.0f) : spawnTime;
    }
};

//NoteType currentNoteType = TAP;