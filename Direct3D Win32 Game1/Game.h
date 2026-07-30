//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"
#include "Note.h"
#include "json.hpp"
#include "imgui.h"
#include "NoteModel.h"
#include "FbxModel.h"
#include <chrono>
#include <PrimitiveBatch.h>
#include <VertexTypes.h>
#include <Effects.h>
#include <CommonStates.h>

#include "sound.h"
#include <memory>
#include <Model.h>

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

enum class BeamState
{
    Homing,
    Fired,
	Resolved
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

//struct HomingBeam {
//    //jsonに保存するデータ群
//    float chargeTime = 0.0f;    //警告開始
//	float lockTime = 0.0f;      //差表確定・発射
//	float hitTime = 0.0f;       //着弾判定
//
//    //実行時に計算する値
//	int lockedSide = 0;        //ロックした側のレーン（-1=左,　0=中央, 1=右）
//    DirectX::SimpleMath::Vector3 lockedPos;
//	BeamState state = BeamState::Homing;
//	bool chargeSoundPlayerd = false;
//	bool fireSoundPlayed = false;
//    bool lockSoundPlayed = false;
//
//    bool judgmentLocked = false;    //早期ロックの判定
//
//    //収束演出用の散らばり候補点
//    static constexpr float kEarlyLockRatio = 0.8f;  // chargeTime~lockTimeのうちこの関数の進んだ時点で実判定を締め切る
//    static constexpr int kScatterCount = 10; //線の数
//    DirectX::SimpleMath::Vector3 scatterOffsets[kScatterCount];
//    bool scatterInitialized = false;
//};

struct HomingBeam {
    float chargeTime = 0.0f;
    float lockTime = 0.0f;
    float hitTime = 0.0f;
    float reloadHit = 0.0f;

    int lockedSide = 0;
    DirectX::SimpleMath::Vector3 lockedPos;
    BeamState state = BeamState::Homing;

    int searchBeatsPlayed = 0;
    bool lockSoundPlayed = false;
    bool fireSoundPlayed = false;

    bool judgmentLocked = false;
    bool fireBeam = false;

    static constexpr int kScatterCount = 15;
    static constexpr int kSearchBeatCount = 3;  //何回サーチ音を鳴らすかついでにkSearchBeatCountの最後の音はロック音にかぶる
    static constexpr float kCountInterval = 0.05f;   //判定を行う間隔
    float nextCountTime = 0.0f; //次に判定を行うまでの時間(.cppで増やす方)
    
    DirectX::SimpleMath::Vector3 scatterOffsets[kScatterCount];
    bool scatterInitialized = false;
};

struct JudggeCircleframe {
    float time; //いつ
    float x;    //どこにいるか
    float y;
    float z;
};

struct JudggeCircle {
    int lane = 0;
    float fixedZ = -210.0f;
    std::vector<JudggeCircleframe> keyframe;
    ID3D11ShaderResourceView* texture = nullptr;

    float currentX = 0.0f;
    float currentY = -55.0f;

    DirectX::SimpleMath::Vector3 GetPosition(float t) const
    {
        if (keyframe.empty()) return { 0.0f,-55.0f,fixedZ };
        if (t <= keyframe.front().time) return { keyframe.front().x, -55.0f, keyframe.front().z };
        if (t >= keyframe.back().time)  return { keyframe.back().x,  -55.0f, keyframe.back().z };

        for (int i = 0; i + 1 < (int)keyframe.size(); i++)
        {
            const auto& a = keyframe[i];
            const auto& b = keyframe[i + 1];
            if (t >= a.time && t <= b.time)
            {
                float ratio = (t - a.time) / (b.time - a.time);
                return{
                    //a.x + (b.x - a.x) * ratio,
                    //a.y + (b.y - a.y) * ratio,
                    //fixedZ
                    a.x + (b.x - a.x) * ratio,
                    a.y + (b.y - a.y) * ratio,
                    a.z + (b.z - a.z) * ratio
                };
            }
        }
        return { keyframe.back().x, keyframe.back().y, keyframe.back().z};
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

    void JudggeLane(char key, int laneIndex);

    void UpdateCamera();

    void InitBeamScatter(HomingBeam& beam);

    bool NoteExit(int lane, float time);

    int GetMirrorLane(int lane);

    // Properties
    void GetDefaultSize(int& width, int& height) const noexcept;

private:
    Sound sound;
    std::string name;
    std::chrono::steady_clock::time_point musicStartTime;
	std::vector<HomingBeam> homingBeams;

    std::unique_ptr<DirectX::PrimitiveBatch<DirectX::VertexPositionColor>> m_primitiveBatch;
    std::unique_ptr<DirectX::PrimitiveBatch<DirectX::VertexPositionColor>> m_yLine;
    std::unique_ptr<DirectX::BasicEffect> m_basicEffect;
    std::unique_ptr<DirectX::CommonStates> m_states;
    std::unique_ptr<NoteModel> m_noteModel;
	std::unique_ptr<FbxModel> m_playerModel;
	std::unique_ptr<FbxModel> m_enemyModel;
    std::unique_ptr<DirectX::IEffectFactory> m_fxFactory;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;

    DirectX::SimpleMath::Vector3 worldPos;
    DirectX::SimpleMath::Vector3 rayDir = {};

    DirectX::SimpleMath::Matrix m_view; //カメラの向き
    DirectX::SimpleMath::Matrix m_proj; //2D変換

    struct LaneKey { char key; int laneIndex; };
    static constexpr LaneKey kLaneKeys[6] = {
        {'S',0},{'D',1},{'F',2},{'J',3},{'K',4},{'L',5}
    };
    struct JudggeEffect
    {
        int rank = -1;  // 0=PERFECT, 1=GREAT, 2=GUD, 3=MISS -1=非表示
        float timer = 0.0f; //何秒表示させるか
    };
    JudggeEffect m_judggeEffects[6];

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
    float m_PlacementHeight = -55.0f; // 配置基準面のY座標(上下方法は後々)

    //ビーム配置UI用の一時入力値
    float m_newBeamChargeTime = 0.0f;
	float m_newBeamLockTime = 1.0f;
	float m_newBeamHitTime = 1.5f;
    //ビームが出るところ（今は敵の位置から）
	DirectX::SimpleMath::Vector3 enemyPos = { 0.0f, 50.0f, 500.0f };
    //クワッド描画用ヘルパー関数　宣言
    void DrawThickBeam(const DirectX::SimpleMath::Vector3& from,
        const DirectX::SimpleMath::Vector3& to,
        float thickness, DirectX::XMVECTORF32 color);

    void DrawThickBeam(const DirectX::SimpleMath::Vector3& from,
        const DirectX::SimpleMath::Vector3& to,
        float thicknessFrom, float thicknessTo,
		DirectX::XMVECTORF32 coreColor,DirectX::XMVECTORF32 glowColor);

    DirectX::SimpleMath::Vector3 cameraPos = { 0.0f, -800.0f, -200.0f };
    // レーン中心(X=0)、判定ラインZ=-210〜出現Z=1000あたりの中間くらいの高さで側面から
    DirectX::XMFLOAT3 m_camSidePos = { 0.0f, 0.0f, 800.0f };  // 400 → 800くらいに
    DirectX::XMFLOAT3 m_camPos = { -555.0f, 700.0f, -65.0f };  // X方向に大きく離す
    DirectX::XMFLOAT3 m_camLook = { -950.0f,  1240.0f, -65.0f };  // 同じZ・Y座標を注視
    //DirectX::XMFLOAT3 m_camLook = { 0.0f,  -55.0f, 400.0f };  // 同じZ・Y座標を注視
    DirectX::XMFLOAT3 m_camUp = { 0.0f,   1.0f,   0.0f };
    float m_camFovy = DirectX::XMConvertToRadians(60.0f);
    float m_camAspect = 16.0f / 9.0f;
    float m_camNear = 1.0f;
    float m_camFar = 5000.0f;
    int m_camState = 0; //0=NONE 1=ORBIT 2=TRACK 3=DOLLY 4=FLIGHT
    POINT m_camOldPos = {};

    int center = 0;
    int totalBeat = 1000;
    int beatsPerMeasure = 4;
    int division = 4;
    int mirrorLane = 3;
    int totalSteps = totalBeat * division;
    int success = 0;
    int perfect = 0;
    int great = 0;
    int good = 0;
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
    bool m_prevLaneKeyDown[6] = { false, false, false, false, false, false };

    BGMstate bgmstate = BGMstate::stop;

    bool mouseDown = GetAsyncKeyState(VK_LBUTTON) & 0x8000;
    bool mousePressed = GetAsyncKeyState(VK_LBUTTON) & 0x0001;
    bool longStart = false;
    bool ViewMode = false;
    float noteTime;
    float breakTime = -100.0f;

    float highSpeed = 0.1f;
    static constexpr float kHighSpeedMin = 0.25f;
    static constexpr float kHighSpeedMax = 10.0f;
    static constexpr float kHighSpeedStep = 0.25f;

    ID3D11ShaderResourceView* noteTexture;
    ID3D11ShaderResourceView* noteTap;
    ID3D11ShaderResourceView* noteLongCenter;
    ID3D11ShaderResourceView* noteLongStart;
    ID3D11ShaderResourceView* noteLongEnd;
    ID3D11ShaderResourceView* noteUntiMirror;
    ID3D11ShaderResourceView* noteMirror;
    ID3D11ShaderResourceView* Beam;

    std::vector<JudggeCircle> judggeCircles;  // 6レーン分

    ID3D11ShaderResourceView* sTexture;
    ID3D11ShaderResourceView* dTexture;
    ID3D11ShaderResourceView* fTexture;
    ID3D11ShaderResourceView* jTexture;
    ID3D11ShaderResourceView* kTexture;
    ID3D11ShaderResourceView* lTexture;

    ID3D11ShaderResourceView* perfectTexture;
    ID3D11ShaderResourceView* greatTexture;
    ID3D11ShaderResourceView* goodTexture;
    ID3D11ShaderResourceView* missTexture;

    //プレイヤーの関連値
    int   playerSide = 0;      // -1:左 / +1:右（判定用、即時反映）
    float playerSideBlend = 1.0f; // 見た目補間用（-1.0〜+1.0）
    DirectX::SimpleMath::Vector3 playerBasePos = { 0.0f, -100.0f, -400.0f }; // 基準位置
    float playerMoveRange = 70.0f; // 左右移動の振れ幅
    float playerVisualX = 0.0f; //見た目用の現在のX座標（補間）
    float playerMoveSpeed = 800.0f; //1秒あたりにどれぐらい動けるか

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