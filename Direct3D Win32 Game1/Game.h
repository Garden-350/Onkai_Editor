//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"
#include "Note.h"
#include "json.hpp"
#include "imgui.h"
#include <chrono>
#include <PrimitiveBatch.h>
#include <VertexTypes.h>
#include <Effects.h>
#include <CommonStates.h>

#include "sound.h"
#include <memory>

enum class BGMstate
{
    stop,
    play,
    pause
};

enum class EditorState
{
    IDLE,
    PLACING_SPAWN,
    PLACING_JUDGE,
    EDITING,
};

struct Beams {
    int lane;
    float time;
    float startTime;
    float endTime;
    float warningTime;

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

    std::unique_ptr<DirectX::PrimitiveBatch<DirectX::VertexPositionColor>> m_primitiveBatch;
    std::unique_ptr<DirectX::BasicEffect> m_basicEffect;
    std::unique_ptr<DirectX::CommonStates> m_states;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;

    DirectX::SimpleMath::Vector3 worldPos;
    DirectX::SimpleMath::Vector3 rayDir = {};

    DirectX::SimpleMath::Matrix m_view; //カメラの向き
    DirectX::SimpleMath::Matrix m_proj; //2D変換

    const int LANE_COUNT = 6;
    int measure = 80;
    float laneWidth = 100.0f;
    float laneStartX = 50.0f;
    float noteHeight = 20.0f;
    float beatHeight = 80.0f;
    float scroll = 0.0f;
    float playSpeed = 300.0f;
    float judgeLine = 500.0f;
    float musicTime = 0.0f;
    float editorTime = 0.0f;
    float scrollSpeed = 0.05f;
    float musicTimeInBeats = 0.0f;
    float editorBeat = 0.0f;
    float LongStartTime = 0.0f;
    float LongInterval = 0.1f;
    float chartPosX = 0.0f;
    float currentBeat = 0.0f;

    DirectX::SimpleMath::Vector3 cameraPos = { 0.0f, 800.0f, -200.0f };

    int center = 0;
    int totalBeat = 1000;
    int beatsPerMeasure = 4;
    int division = 4;
    int mirrorLane = 3;
    int totalSteps = totalBeat * division;
    int success = 0;
    int miss = 0;
    int bpm = 120;
    int LongStartLane = 0;
    int selectedNoteIndex = -1;

    bool fastLong = false;
    bool fastTime = false;
    bool musicPlaying = true;
    bool musicPause = false;
    bool mirrorMode = false;
    bool isPlaying = false;
    bool isLong = false;
    bool oknort = false;
    bool KeySpacePrev = false;
    bool LongStartPlaced = false;
    bool isOutRange = false;

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

    float SnapToGrid(float time, float bpm, int division);

    // Device resources.
    std::unique_ptr<DX::DeviceResources>    m_deviceResources;

    EditorState m_EditorState = EditorState::IDLE;
    Note m_TempNote;

    float m_ChartW = 0.0f;
    float m_ChartH = 0.0f;

    // 3D Path Preview ミニ枠（Render()で算出し、Update()のクリック判定で使う）
    ImVec2 m_PreviewMin = { 0.0f, 0.0f };
    ImVec2 m_PreviewMax = { 0.0f, 0.0f };
    ImVec2 m_PreviewCenter = { 0.0f, 0.0f };
    bool m_PreviewValid = false; // Render()で一度計算されたかどうか

    // Rendering loop timer.
    DX::StepTimer                           m_timer;
};