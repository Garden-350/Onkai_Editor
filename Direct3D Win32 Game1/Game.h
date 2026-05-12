//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"
#include "Note.h"
#include <chrono>

#include "sound.h"

enum class BGMstate
{
    stop,
    play,
    pause
};

struct Beams {
    int lane;           // どのレーンに出るか
    float time;
    float startTime;    // ビームが始まる時刻（秒）
    float endTime;      // ビームが終わる時刻（秒）
    float warningTime;  // 予告線が出始める時刻（startTimeより前）

    Beams(int l, float start, float end, float warning)
        : lane(l), startTime(start), endTime(end), warningTime(warning) {
    }
};

// A basic game implementation that creates a D3D11 device and
// provides a game loop.
class Game final : public DX::IDeviceNotify
{
public:

    Game() noexcept(false);
    ~Game();

    Game(Game&&) = default;
    Game& operator= (Game&&) = default;

    Game(Game const&) = delete;
    Game& operator= (Game const&) = delete;

    // Initialization and management
    void Initialize(HWND window, int width, int height);

    // Basic game loop
    void Tick();

    // IDeviceNotify
    void OnDeviceLost() override;
    void OnDeviceRestored() override;

    // Messages
    void OnActivated();
    void OnDeactivated();
    void OnSuspending();
    void OnResuming();
    void OnWindowMoved();
    void OnDisplayChange();
    void OnWindowSizeChanged(int width, int height);
    void SaveChart(std::string name);
    void LoadChart(std::string name);

    void NoteBorn();
    void JudgeNoteS();
    void JudgeNoteD();
    void JudgeNoteF();
    void JudgeNoteJ();
    void JudgeNoteK();
    void JudgeNoteL();
    void JudgeLongNoteS();
    void JudgeLongNoteD();
    void JudgeLongNoteF();
    void JudgeLongNoteJ();
    void JudgeLongNoteK();
    void JudgeLongNoteL();

    bool NoteExit(int lane, float time);

    int GetMirrorLane(int lane);

    // Properties
    void GetDefaultSize(int& width, int& height) const noexcept;

private:
    Sound sound;
    std::string name;
    std::chrono::steady_clock::time_point musicStartTime;

    const int LANE_COUNT = 6;
    int measure = 80;           //曲の小節数。調整必須
    float laneWidth = 100.0f;
    float laneStartX = 50.0f;
    float noteHeight = 20.0f;
    float beatHeight = 80.0f;   // 1拍の高さ
    float scroll = 0.0f;
    float playSpeed = 300.0f;
    float judgeLine = 500.0f;
    float musicTime = 0.0f;
    float editorTime = 0.0f;
    float scrollSpeed = 0.05f;
    float musicTimeInBeats = 0.0f;
    float editorBeat = 0.0f;
    float LongStartTime = 0.0f;
    float LongInterval = 0.125f;    //8分音符間隔

    float currentBeat = 0.0f;

    int center = 0;
    int totalBeat = 1000;
    int beatsPerMeasure = 4;    // 1小節の拍
    int division = 4;           // 拍の分割(16分)
    int mirrorLane = 3;         //初期ミラー配置
    int totalSteps = totalBeat * division;
    int success = 0;            //判定のカウント
    int miss = 0;               //ミスのカウント
    int bpm;
    int LongStartLane = 0;

    bool fastLong = false;
    bool fastTime = false;
    bool musicPlaying = true;
    bool musicPause = false;
    bool mirrorMode = false;
    bool isPlaying = false;
    bool isLong = false;
    bool oknort = false;
    bool KeySpacePrev = false;
    bool LongStartPlaced = false;   //ロングノーツの最初判定(置くとき)

    BGMstate bgmstate = BGMstate::stop;

    bool mouseDown = GetAsyncKeyState(VK_LBUTTON) & 0x8000;
    bool mousePressed = GetAsyncKeyState(VK_LBUTTON) & 0x0001;
    bool longStart = false;
    float noteTime;
    float breakTime = -100.0f;

    ID3D11ShaderResourceView* noteTexture;
    ID3D11ShaderResourceView* noteTap;
    ID3D11ShaderResourceView* noteLongCenter;
    ID3D11ShaderResourceView* noteLongStart;
    ID3D11ShaderResourceView* noteLongEnd;
    ID3D11ShaderResourceView* noteUntiMirror;
    ID3D11ShaderResourceView* noteMirror;
    ID3D11ShaderResourceView* Beam;

    void Update(DX::StepTimer const& timer);
    void Render();

    void Clear();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();

    // Device resources.
    std::unique_ptr<DX::DeviceResources>    m_deviceResources;


    // Rendering loop timer.
    DX::StepTimer                           m_timer;
};