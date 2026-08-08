//
// Game.cpp
//

#include "pch.h"
#include "Game.h"
#include "imgui.h"
#include "sound.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "WICTextureLoader.h"

extern void ExitGame() noexcept;
#include <vector>
#include <fstream>
#include <sstream>
#include <Audio.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <SpriteBatch.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// ─────────────────────────────────────────────
// グローバル変数
// ─────────────────────────────────────────────
std::vector<Note>  notes;          // 現在配置されているノーツ一覧
std::vector<Beams> beams;          // ビームオブジェクト一覧

std::vector<Note>  originalNotes;  // 再生前のノーツ状態を退避しておくバックアップ
static char filename[64] = "chartname"; // セーブ／ロード時のファイル名バッファ
static std::string g_chartStatusMessage;    //save/loadの結果メッセ
static bool g_chartStatusIsError = false;   //成功：false 失敗: true
//static float g_defaultTravelTime = 1.2f;
//static float g_noteSpeed = 700.0f; // ノーツの移動速度（ワールド単位/秒）。大きいほど速く飛んでくる
//static float g_minTravelTime = 0.3f; // 距離が近すぎる場合の最低保証時間（秒）。0除算防止も兼ねる
static float g_approachBeats = 2.0f;   // 判定ラインに到達するまで何拍分の余裕を持たせるか
static float g_minTravelTime = 0.3f;   // 距離が近すぎる/BPMが低すぎる場合の最低保証時間（0除算防止も兼ねる）

NoteType currentNoteType = TAP;    // 現在選択中のノーツ種別


int lane;                          // 汎用レーン番号（現在は未使用）
static float beamLenght = 2.0f;   // ビームの長さ（現在は未使用）

using namespace DirectX;
using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

auto previoursTime = std::chrono::high_resolution_clock::now(); // 経過時間計測用（未使用）

ImVec2 m_EditorScreenPos = { 0, 0 }; // エディタ画面の左上座標（ImGui 描画位置）

// ─────────────────────────────────────────────
// 「3D Path Preview」ミニウィンドウ用 射影係数
//
//   Project3D(v) の計算式:
//     sx = previewCenter.x + v.x * kPreviewScaleX  + v.z * kPreviewScaleXZ
//     sy = previewCenter.y - v.y * kPreviewScaleY  - v.z * kPreviewScaleYZ
//
//   ノーツパスを左パネル内の小さなプレビュー(約200px)へ
//   等角投影（アイソメトリック近似）で描画するための定数。
// ─────────────────────────────────────────────
constexpr float kPreviewScaleX = 0.1469f;
constexpr float kPreviewScaleXZ = 0.0351f;
constexpr float kPreviewScaleY = 0.3512f;
constexpr float kPreviewScaleYZ = 0.0543f;

//判定タイミング(調整必須)
constexpr float kPerfect = 0.02f;
constexpr float kGreat = 0.04f;
constexpr float kGud = 0.06f;

//ロングノーツのティックインターバル
constexpr float kLongTickInterval = 0.2f;

// ─────────────────────────────────────────────
// LerpVector3 : 2つのVector3を線形補間する
//   a, b : 補間元・補間先
//   t    : 補間係数 [0.0 ～ 1.0]
// ─────────────────────────────────────────────
static DirectX::SimpleMath::Vector3 LerpVector3(
    const DirectX::SimpleMath::Vector3& a,
    const DirectX::SimpleMath::Vector3& b,
    float t)
{
    return a + (b - a) * t;
}

// ─────────────────────────────────────────────
// EvaluateNotePosition : 指定時刻 now におけるノーツの3D座標を返す
//
//   ノーツは spawnPos(出現位置) → judgePos(判定位置) へ移動する。
//   controlPoints の数によってパスの種類が変わる:
//     0個 : 直線 (Linear)
//     1個 : 2次ベジェ曲線 (Quadratic Bezier)
//     2個 : 3次ベジェ曲線 (Cubic Bezier)
//
//   ※ t は spawnTime〜time の範囲で正規化された値
// ─────────────────────────────────────────────
static DirectX::SimpleMath::Vector3 EvaluateNotePosition(
    const Note& note, float now)
{
    float startTime = note.GetSpawnTime();
    float endTime = note.time;
    float duration = endTime - startTime;
    if (duration < 0.0001f) duration = 0.0001f; // ゼロ除算防止

    // 正規化された補間係数 t を [0,1] にクランプ
    float t = (now - startTime) / duration;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    const auto& p0 = note.spawnPos;
    const auto& p3 = note.judgePos;

    // 制御点なし → 直線補間
    if (note.controlPoints.empty())
        return LerpVector3(p0, p3, t);

    // 制御点1個 → 2次ベジェ
    if (note.controlPoints.size() == 1)
    {
        const auto& p1 = note.controlPoints[0];
        return LerpVector3(LerpVector3(p0, p1, t), LerpVector3(p1, p3, t), t);
    }

    // 制御点2個 → 3次ベジェ（De Casteljau アルゴリズム）
    const auto& p1 = note.controlPoints[0];
    const auto& p2 = note.controlPoints[1];
    auto a = LerpVector3(p0, p1, t);
    auto b = LerpVector3(p1, p2, t);
    auto c = LerpVector3(p2, p3, t);
    return LerpVector3(LerpVector3(a, b, t), LerpVector3(b, c, t), t);
}

static DirectX::SimpleMath::Vector3 EvaluateLongNotePos(const Note& note, float now)
{
    float startTime = note.GetSpawnTime();
    float arraiveTime = note.arrivalTime;
	float duration = arraiveTime - startTime;
	if (duration < 0.0001f) duration = 0.0001f; // ゼロ除算防止

	float t = (now - startTime) / duration; //not クランプ
	return LerpVector3(note.spawnPos, note.judgePos, t);
}


// ─────────────────────────────────────────────
// Game コンストラクタ
//   DeviceResources（D3D11デバイス管理クラス）を生成する。
// ─────────────────────────────────────────────
Game::Game() noexcept(false)
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();
    m_deviceResources->RegisterDeviceNotify(this);
}

// ─────────────────────────────────────────────
// Game デストラクタ
//   ImGui のシャットダウン処理を行う。
// ─────────────────────────────────────────────
Game::~Game()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
// ─────────────────────────────────────────────
// LoadTextureChecked : テクスチャをロードし、失敗時はデバッグ出力にログを残す
// ─────────────────────────────────────────────
static void LoadTextureChecked(ID3D11Device* dev, const wchar_t* path, ID3D11ShaderResourceView** out)
{
    HRESULT hr = DirectX::CreateWICTextureFromFile(dev, path, nullptr, out);
    if (FAILED(hr))
    {
        wchar_t buf[256];
        swprintf_s(buf, L"[TextureLoad FAILED] %s  HRESULT=0x%08X\n", path, hr);
        OutputDebugStringW(buf);
    }
    else
    {
        wchar_t buf[256];
        swprintf_s(buf, L"[TextureLoad OK] %s\n", path);
        OutputDebugStringW(buf);
    }
}

static ImVec2 RotatiePoint(ImVec2 p, ImVec2 center, float angleRad)
{
    float s = sinf(angleRad), c = cosf(angleRad);
    float dx = p.x - center.x;
    float dy = p.y - center.y;
    return ImVec2(center.x + dx * c - dy * s, center.y + dy * s + dy * c);
}

static void AddRotatedSaure(ImDrawList* draw, ImVec2 center, float halfSize,
    float angleRad, ImU32 col, float thickness = 0.0f, bool filed = false)
{
    ImVec2 corners[4] = {
        ImVec2(center.x - halfSize, center.y - halfSize),
        ImVec2(center.x + halfSize, center.y - halfSize),
        ImVec2(center.x + halfSize, center.y + halfSize),
        ImVec2(center.x - halfSize, center.y + halfSize),
    };
    for (int i = 0; i < 4; i++)
        corners[i] = RotatiePoint(corners[i], center, angleRad);

    if (filed)
        draw->AddConvexPolyFilled(corners, 4, col);
    else
        draw->AddPolyline(corners, 4, col, ImDrawFlags_Closed, thickness);
}

static void DrawJuddgeBurst(ImDrawList* draw, ImVec2 center, float age, int rank)
{
    //age : 0.0(発生直後) - 1.0(消滅直前)
    float growT = 1.0f - powf(1.0f - age, 3.0f);    // ease-out（最初速く、後でゆっくり広がる）
    float fade = 1.0f - age;

    //ImU32 coreCol, rayCol, tipCol;
    ImU32 coreCol, ringCol, orbitCol;
    float maxRadius;
    float maxLen;
    int rayCount;
    int orbitCount;

    switch (rank)
    {
    case 0: // PERFECT
        coreCol = IM_COL32(255, 255, 150, (int)(255 * fade));
        ringCol = IM_COL32(255, 220, 40, (int)(230 * fade));
        orbitCol = IM_COL32(255, 140, 40, (int)(200 * fade));
        maxRadius = 155.0f; orbitCount = 6;
        break;
    case 1: // GREAT
        coreCol = IM_COL32(180, 255, 200, (int)(255 * fade));
        ringCol = IM_COL32(120, 255, 160, (int)(220 * fade));
        orbitCol = IM_COL32(60, 200, 255, (int)(200 * fade));
        maxRadius = 142.0f; orbitCount = 5;
        break;
    default: // GOOD
        coreCol = IM_COL32(200, 220, 255, (int)(255 * fade));
        ringCol = IM_COL32(150, 190, 255, (int)(200 * fade));
        orbitCol = IM_COL32(150, 190, 255, (int)(160 * fade));
        maxRadius = 132.0f; orbitCount = 4;
        break;
    }

    //中心のコア四角　：　発生直後だけ大きく光、収縮しながら回転
    float coreHalf = 14.0f * (1.0f - growT) + 3.0f;
    float coreSpin = age * 4.0f;    //ゆっくり回転
    AddRotatedSaure(draw, center, coreHalf, coreSpin, coreCol, 0.0f, true);

    //メインリング　：　高速回転しながら外側へ広がる
    float ringHalf = maxRadius * growT;
    float ringSpin = age * 10.0f;   //数値を上げれば速くなる
    AddRotatedSaure(draw, center, ringHalf, ringSpin, ringCol, 2.5f * fade + 0.5f, false);

    //二重リング　：　少し遅れて逆回転させる
    float ringHalf2 = maxRadius * growT * 0.6f;
    AddRotatedSaure(draw, center, ringHalf2, -ringSpin * 1.4f, ringCol, 1.5f * fade + 0.5f, false);

    //中心の周りを公転する四角s
    for (int i = 0; i < orbitCount; i++)
    {
        float baseAngle = (float)i / orbitCount * 6.2831853;
        float orbitAngle = baseAngle + age * 6.0f;
        float orbitRadius = maxRadius * growT;

        ImVec2 orbitCenter(
            center.x + cosf(orbitAngle) * orbitRadius,
            center.y + sinf(orbitAngle) * orbitRadius
        );
        float smallHalf = 5.0f * fade + 1.0f;
        float selfSpin = age * 8.0f + baseAngle;    //自転
        AddRotatedSaure(draw, orbitCenter, smallHalf, selfSpin, orbitCol, 0.0f, true);
    }


    //float coreRadius = 10.0f * (1.0f - growT * 0.5f) + 2.0f;
    //draw->AddCircleFilled(center, coreRadius, coreCol);

    //for (int i = 0; i < rayCount; i++)
    //{
    //    float baseAngle = (float)i / rayCount * 6.2831853f;
    //    float jitter = sinf((float)i * 12.9898f) * 0.25f;   //疑似ランダムな揺らぎ
    //    float angle = baseAngle + jitter;

    //    float len = maxLen * growT * (0.6f + 0.4f * fabsf(sinf(i * 3.71f)));
    //    ImVec2 dir(cosf(angle), sinf(angle));
    //    ImVec2 p0(center.x + dir.x * coreRadius, center.y + dir.y * coreRadius);
    //    ImVec2 p1(center.x + dir.x * len, center.y + dir.y * len);

    //    draw->AddLine(p0, p1, rayCol, 2.5f * fade + 0.5f);
    //    draw->AddCircleFilled(p1, 2.0f * fade, tipCol);
    //}
}

float Game::CalcFacingAngleY(const DirectX::SimpleMath::Vector3& from, const DirectX::SimpleMath::Vector3& to)
{
    Vector3 dir = to - from;
    dir.y = 0.0f;
	if (dir.LengthSquared() < 0.0001f) return 0.0f;
	dir.Normalize();
    return atan2f(dir.x, dir.z);
}

// ─────────────────────────────────────────────
// Initialize : D3D11リソース初期化 & ImGui セットアップ
//   window : ウィンドウハンドル
//   width, height : ウィンドウサイズ
// ─────────────────────────────────────────────
void Game::Initialize(HWND window, int width, int height)
{
    sound.Init(); // サウンドエンジン初期化

    m_deviceResources->SetWindow(window, width, height);
    m_deviceResources->CreateDeviceResources();
    CreateDeviceDependentResources();
    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();
    //判定円テクスチャロード
    ID3D11Device* dev = m_deviceResources->GetD3DDevice();
    LoadTextureChecked(dev, L"asset/S.png", &sTexture);
    LoadTextureChecked(dev, L"asset/D.png", &dTexture);
    LoadTextureChecked(dev, L"asset/F.png", &fTexture);
    LoadTextureChecked(dev, L"asset/J.png", &jTexture);
    LoadTextureChecked(dev, L"asset/K.png", &kTexture);
    LoadTextureChecked(dev, L"asset/L.png", &lTexture);
    LoadTextureChecked(dev, L"asset/PER.png", &perfectTexture);
    LoadTextureChecked(dev, L"asset/GRE.png", &greatTexture);
    LoadTextureChecked(dev, L"asset/GOO.png", &goodTexture);
    LoadTextureChecked(dev, L"asset/MIS.png", &missTexture);
    //ほかのテクスチャもこのしたに追加
    
    //==================================
    judggeCircles.resize(6); //あとでテクスチャしっかり用意できたら6に変更
    ID3D11ShaderResourceView* texList[6] = {
        sTexture, dTexture, fTexture, jTexture, kTexture, lTexture,/*d,f,j,k,l,をあとで追加*/
    };
    for (int i = 0; i < 6; i++)
    {
        judggeCircles[i].lane = i;
        judggeCircles[i].texture = texList[i];
        judggeCircles[i].fixedZ = -210.0f;

        //デフォルトキーフレーム
        float defaultX = -( - 240.0f + (540.0f / 6.0f) * i + (600.0f / 12.0f));
        judggeCircles[i].keyframe.push_back({ 0.0f,defaultX,-55.0f, -210.0f });
    }

    //3Dモデルのロード
    m_noteModel = std::make_unique<NoteModel>();
    if (!m_noteModel->Load(dev, L"asset/Model/note2/music_sord_1.obj", L"asset/Model/note2"))
    {
        OutputDebugStringW(L"[Game] NoteModel load failed\n");
        m_noteModel.reset();    //失敗時はnullptrに戻す
    }

	m_flickModel = std::make_unique<FbxModel>();
    //横フリックのモデルに差し替えてね
    if(!m_flickModel->Load(dev, L"asset/Model/Flick/allow.fbx", L"asset/Model/Flick"))
    {
        OutputDebugStringW(L"[Game] FlickModel load failed\n");
        m_flickModel.reset();
	}

	m_playerModel = std::make_unique<FbxModel>();
    if(!m_playerModel->Load(dev,L"asset/Model/Player/player.fbx", L"asset/Model/Player"))
    {
        OutputDebugStringW(L"[Game] PlayerModel load failed\n");
        m_playerModel.reset();
	}
    m_enemyModel = std::make_unique<FbxModel>();
    if (!m_enemyModel->Load(dev, L"asset/Model/Enemy/enemy.fbx", L"asset/Model/Enemy"))
    {
        OutputDebugStringW(L"[Game] EnemyModel load failed\n");
        m_enemyModel.reset();
    }

	m_Long_Sentan_Model = std::make_unique<FbxModel>();
    if(!m_Long_Sentan_Model->Load(dev, L"asset/Model/kusari/kusari_sentan_2.fbx", L"asset/Model/kusari_sentan_2"))
    {
        OutputDebugStringW(L"[Game] Long_Sentan_Model load failed\n");
        m_Long_Sentan_Model.reset();
	}

    m_Long_Dou_Model = std::make_unique<FbxModel>();
    if (!m_Long_Dou_Model->Load(dev, L"asset/Model/kusari/kusari_dou.fbx", L"asset/Model/kusari_dou"))
    {
        OutputDebugStringW(L"[Game] Long_Dou_Model load failed\n");
        m_Long_Dou_Model.reset();
    }

	m_Long_O_Model = std::make_unique<FbxModel>();
    if (!m_Long_O_Model->Load(dev, L"asset/Model/kusari/kusari_o_2.fbx", L"asset/Model/kusari_o_2"))
    {
        OutputDebugStringW(L"[Game] Long_O_Model load failed\n");
        m_Long_O_Model.reset();
    }


    // ※ 3Dモデルのロード情報（現在は未使用）
    //struct Load3DInfo {
    //    std::string filename;
    //    std::string texdirectoryname;
    //    Load3DInfo(std::string p1, std::string p2) {
    //        filename = p1;
    //        texdirectoryname = p2;
    //    }
    //};

    //std::array<Load3DInfo, 1> g_loadmodel =
    //{
    //    Load3DInfo("asset/Model/Dai/Daiza.obj", "asset/Model"),
    //};



    // ImGui 初期化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // キーボードナビゲーション有効
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // ゲームパッドナビゲーション有効

    ImGui_ImplWin32_Init(window);
    ID3D11Device* device = m_deviceResources->GetD3DDevice();
    ID3D11DeviceContext* context = m_deviceResources->GetD3DDeviceContext();
    ImGui_ImplDX11_Init(device, context);
}

#pragma region Frame Update

// ─────────────────────────────────────────────
// Tick : メインゲームループ（毎フレーム呼ばれる）
//   ImGui のフレーム開始 → Update → Render の順で処理する。
// ─────────────────────────────────────────────
void Game::Tick()
{
    // ImGui 新フレーム開始
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    m_timer.Tick([&]()
        {
            Update(m_timer);
        });

    Render();
}

DirectX::SimpleMath::Vector3 Game::EvaluteChainPartPosition(const Note& note, float now, float leadOffset)
{
    float virtualTime = now + leadOffset; // 先行してる分、時間を進めて評価する
    return EvaluateLongNotePosition(note, virtualTime);
}

DirectX::SimpleMath::Vector3 Game::EvaluateLongNotePosition(const Note& note, float now)
{
    float startTime = note.GetSpawnTime();
    float endTime = note.time;
    float duration = endTime - startTime;
    if (duration < 0.0001f) duration = 0.0001f;

    float t = (now - startTime) / duration; // クランプしない

    const auto& p0 = note.spawnPos;
    const auto& p3 = note.judgePos;
    return LerpVector3(p0, p3, t);
}

// ─────────────────────────────────────────────
// Update : 毎フレームのゲームロジック更新
//   ・マウス入力の取得
//   ・キーボード操作（カメラ移動・モード切替など）
//   ・エディタ状態機械（ノーツ配置 / 選択 / 削除）
//   ・BGM 再生状態管理
// ─────────────────────────────────────────────
void Game::Update(DX::StepTimer const& timer)
{
    // ─ マウスボタン状態の取得 ─────────────────
    // mousePressed : このフレームで左クリックされた瞬間（エッジ検出）
    if (GetAsyncKeyState(VK_LBUTTON) & 0x0001)
        mousePressed = true;
    else
        mousePressed = false;

    // mouseDown : 左ボタンを押し続けている間 true
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        mouseDown = true;
    else
        mouseDown = false;

    //判定円の位置にホーミング
    {
        float now = (bgmstate == BGMstate::play)
            ? musicTime : editorBeat * (60.0f / bpm);

        for (auto& note : notes)
        {
			if (note.note == FLICK_RIGHT || note.note == FLICK_LEFT) continue; //フリックは判定円にホーミングしない
            if (note.lane < 0 || note.lane >= (int)judggeCircles.size()) continue;
            const auto& circle = judggeCircles[note.lane];
            auto circlePos = circle.GetPosition(now);
            note.judgePos = circlePos;
        }
    }

    float elapsedTime = float(timer.GetElapsedSeconds());

    // ─ Q/W キー : oknort フラグ切替 ──────────
    // Q で ON、W で OFF（用途は未実装）
    //if (GetAsyncKeyState('Q') & 0x0001) oknort = true;
    //if (GetAsyncKeyState('W') & 0x0001 && oknort == true) oknort = false;

    // ─ Z/X キー : ミラーモード切替 ────────────
    // Z で ON（左右レーン反転）、X で OFF
    if (GetAsyncKeyState('Z') & 0x0001) mirrorMode = true;
    if (GetAsyncKeyState('X') & 0x0001 && mirrorMode == true) mirrorMode = false;

    //ずっと動くのいやだからビームが来る何フレームか前だけ動けるとかにした方がいいかも
    if (GetAsyncKeyState('S') & 0x8000 || GetAsyncKeyState('D') & 0x8000 || GetAsyncKeyState('F') & 0x8000)
        playerSide = 1;
    else if (GetAsyncKeyState('J') & 0x8000 || GetAsyncKeyState('K') & 0x8000 || GetAsyncKeyState('L') & 0x8000)
        playerSide = -1;
    else
        playerSide = 0;

    //移動の補間
    {
        float targetX = playerSide * playerMoveRange;
        float diff = targetX - playerVisualX;

        float maxStep = playerMoveSpeed * elapsedTime;  //このフレームで動ける最大距離

        if (fabs(diff) <= maxStep)
            playerVisualX = targetX;    //目標に十分近ければ合わせる
        else
            playerVisualX += (diff > 0.0f ? maxStep : -maxStep);
    }

    // ロングノーツの判定間隔スライダー（ImGui UI）
    ImGui::SliderFloat("Long Interval", &LongInterval, 0.03f, 0.5f);

    // ロングノーツ配置中の案内テキスト
    if (LongStartPlaced)
        ImGui::TextColored(ImVec4(1, 1, 0, 1), ">>> START placed! Click END position <<<");

    // ─ マウスクリックによるノーツ配置 / 選択 ──
    if (!isOutRange) // マウスが編集エリア内にある場合のみ処理
    {
        // ── レイキャスト：スクリーン座標 → ワールド座標 変換 ──────────
        //
        //   手順:
        //     1. マウス座標をスクリーン正規化座標 (NDC: -1〜+1) に変換
        //     2. View*Proj の逆行列で NDC → ワールド座標へ逆変換
        //     3. Near 面の点と Far 面の点からレイ方向を求める
        //     4. レイと y=-55 平面（ノーツが置かれる床）の交点を worldPos として取得
        //
        ImVec2 mousePos = ImGui::GetMousePos();
        float mouseX = mousePos.x;
        float mouseY = mousePos.y;
        float screenW = ImGui::GetIO().DisplaySize.x;
        float screenH = ImGui::GetIO().DisplaySize.y;

        // NDC 変換（DirectX は Y 軸が上向き正なので 1-y/h）
        float ndcX = (mouseX / screenW) * 2.0f - 1.0f;
        float ndcY = 1.0f - (mouseY / screenH) * 2.0f;

        // ビュー・プロジェクション行列の逆行列でアンプロジェクション
        DirectX::SimpleMath::Matrix viewProj = m_view * m_proj;
        DirectX::SimpleMath::Matrix viewProjInv = viewProj.Invert();

        // NDC z=0(Near面) と z=1(Far面) をワールド座標へ変換
        DirectX::SimpleMath::Vector3 nearPoint = DirectX::SimpleMath::Vector3::Transform(
            DirectX::SimpleMath::Vector3(ndcX, ndcY, 0.0f), viewProjInv);
        DirectX::SimpleMath::Vector3 farPoint = DirectX::SimpleMath::Vector3::Transform(
            DirectX::SimpleMath::Vector3(ndcX, ndcY, 1.0f), viewProjInv);

        // レイ方向（正規化済み）
        rayDir = farPoint - nearPoint;
        rayDir.Normalize();

        DirectX::SimpleMath::Vector3 camForward = DirectX::SimpleMath::Vector3(m_camLook) - DirectX::SimpleMath::Vector3(m_camPos);
        camForward.Normalize();
        constexpr float PlaneDistance = 500.0f; //  カメラからの距離（任意調整）
        DirectX::SimpleMath::Vector3 planePoint =
            DirectX::SimpleMath::Vector3(m_camPos) + camForward * PlaneDistance;

        float denom = rayDir.Dot(camForward);
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        {
            m_PlacementHeight += ImGui::GetIO().MouseWheel * 10.0f; // 感度は調整
        }
        // y=-55 の水平面とレイの交点を計算
        constexpr float noteY = -55.0f;
        bool hasHit = false;
        if (fabsf(denom) > 1e-5f) // y成分がほぼ0でなければ（水平レイは交差しない）
        {
            //float t = (planePoint - nearPoint).Dot(camForward) / denom;
            //float t = (noteY - nearPoint.y) / rayDir.y; // 交点パラメータ
            float t = (m_PlacementHeight - nearPoint.y) / rayDir.y;
            if (t > 0.0f) // カメラ後方には交差しない
            {
                worldPos = nearPoint + rayDir * t; // 交点座標（ワールド）
                hasHit = true;
            }
        }

        // ── 左クリック時の処理：エディタ状態機械 ──────────────────────
        if (mousePressed && hasHit)
        {
            float worldX = worldPos.x;
            float worldZ = worldPos.z;

            switch (m_EditorState)
            {
            case EditorState::IDLE:
                // ノーツ選択モード：クリック位置に近いノーツを探す
            {
                selectedNoteIndex = -1;
                for (int i = 0; i < (int)notes.size(); i++)
                {
                    const auto& sp = notes[i].spawnPos;
                    float dx = worldX - sp.x;
                    float dz = worldZ - sp.z;
                    // 半径 40 ワールド単位以内なら選択
                    if (dx * dx + dz * dz < 40.0f * 40.0f)
                    {
                        selectedNoteIndex = i;
                        m_EditorState = EditorState::EDITING;
                        break;
                    }
                }
            }
            break;

            case EditorState::PLACING_SPAWN:
                // ノーツ配置ステップ1：出現位置(spawnPos)を決定
                m_TempNote.spawnPos = { worldX, worldPos.y, worldZ };
                if (m_TempNote.note == FLICK_RIGHT || m_TempNote.note == FLICK_LEFT)
                {
                    m_TempNote.spawnPos = { enemyPos.x, enemyPos.y, enemyPos.z }; // ← 敵の位置に固定
                }
                m_EditorState = EditorState::PLACING_JUDGE; // 次は判定位置
                break;

            case EditorState::PLACING_JUDGE:
                // ノーツ配置ステップ2：判定位置(judgePos)を決定 → ノーツをリストへ追加
                m_TempNote.judgePos = { worldX, worldPos.y, worldZ };
                if (m_TempNote.note == FLICK_RIGHT || m_TempNote.note == FLICK_LEFT)
                    m_TempNote.judgePos = { 0.0f, m_TempNote.judgePos.y, -210.0f }; // ← 到達位置はX=0, Z=-210固定

                // ── BPM連動で移動時間を決定 ──
                // 「g_approachBeats拍前」に出現するようにtimeを逆算する。
                // これにより、距離やBPMに関わらず常に同じ体感速度で降ってくる。
                {
                    float secondsPerBeat = (bpm > 0.0f) ? 60.0f / bpm : 0.5f;
                    float travelTime = g_approachBeats * secondsPerBeat;
                    if (travelTime < g_minTravelTime) travelTime = g_minTravelTime; // 極端に速いBPM等での保険
                    m_TempNote.time = m_TempNote.spawnTime + travelTime;
                }

                notes.push_back(m_TempNote);
                // 判定時刻でソート（常に昇順を維持）
                std::sort(notes.begin(), notes.end(),
                    [](const Note& a, const Note& b) { return a.time < b.time; });
                selectedNoteIndex = (int)notes.size() - 1;
                m_EditorState = EditorState::EDITING;
                break;

            default:
                break;
            }
        }

        // ── 右クリック：配置キャンセル or ノーツ削除 ─────────────────
        if (GetAsyncKeyState(VK_RBUTTON) & 0x0001)
        {
            if (m_EditorState == EditorState::PLACING_JUDGE ||
                m_EditorState == EditorState::PLACING_SPAWN)
            {
                // 配置中なら中断
                m_EditorState = EditorState::IDLE;
            }
            else
            {
                // 選択中ノーツを削除
                if (selectedNoteIndex >= 0 && selectedNoteIndex < (int)notes.size())
                {
                    notes.erase(notes.begin() + selectedNoteIndex);
                    selectedNoteIndex = -1;
                    m_EditorState = EditorState::IDLE;
                }
            }
        }

        // ESC：選択解除・IDLE に戻る
        if (GetAsyncKeyState(VK_ESCAPE) & 0x0001)
        {
            m_EditorState = EditorState::IDLE;
            selectedNoteIndex = -1;
        }
    }

    // ─ カメラ移動（J/L: X軸  I/K: Y軸  U/O: Z軸）──────────────────
    UpdateCamera();

    // （ノーツ判定関数は現在コメントアウト中）

    // ─ ミラー基準レーン変更（← / → キー）────────────────────────
    if (GetAsyncKeyState(VK_LEFT) & 0x0001) mirrorLane--;
    if (GetAsyncKeyState(VK_RIGHT) & 0x0001) mirrorLane++;
    if (mirrorLane < 0)          mirrorLane = 0;
    if (mirrorLane >= LANE_COUNT) mirrorLane = LANE_COUNT;

    // マウスホイールでエディタのスクロール量を更新
    //scroll += ImGui::GetIO().MouseWheel * 40.0f;

    // ─ スペースキー：BGM 再生 / 一時停止 / 再開 ────────────────────
    static bool KeySpacePrev = false;
    bool keySpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

    if (keySpace && !KeySpacePrev) // キーを押した瞬間だけ実行
    {
        switch (bgmstate)
        {
        case BGMstate::stop:
            // 再生開始：ノーツ状態を退避し、エディタ位置から BGM 再生
            originalNotes = notes;
            musicTime = editorBeat * (60.0f / bpm);
            sound.SetPlayPositionSeconds(SOUND_LABEL_BGM001, musicTime);
            sound.Play(SOUND_LABEL_BGM001);
            bgmstate = BGMstate::play;
            break;

        case BGMstate::play:
            // 一時停止：ノーツを退避前の状態に戻す
            notes = originalNotes;
            success = 0;
            miss = 0;
            sound.Pause(SOUND_LABEL_BGM001);
            bgmstate = BGMstate::pause;
            break;

        case BGMstate::pause:
            // 再開：BGM の再生を再スタート
            sound.Resume(SOUND_LABEL_BGM001);
            bgmstate = BGMstate::play;
            if (mirrorMode == true) mirrorMode = !mirrorMode;
            break;
        }
    }

    if (bgmstate == BGMstate::play)
    {
        for (auto& beam : homingBeams)
        {
            // ── タンタンタン：chargeTime~lockTimeを3等分した時刻に鳴らす ──
            {
                float total = beam.lockTime - beam.chargeTime;
                if (total < 0.01f) total = 0.01f;
                float beatInterval = total / HomingBeam::kSearchBeatCount;

                while (beam.searchBeatsPlayed < HomingBeam::kSearchBeatCount)
                {
                    float beatTime = beam.chargeTime + beatInterval * beam.searchBeatsPlayed;
                    if (musicTime >= beatTime)
                    {
                        sound.Play(SOUND_BEAM_SARCH); // タン
                        beam.searchBeatsPlayed++;
                    }
                    else
                    {
                        break;
                    }
                }
            }

            if (beam.state == BeamState::Homing)
            {
                // カン：タンを3回鳴らし終えた瞬間に即ロック確定
                if (!beam.judgmentLocked && beam.searchBeatsPlayed >= HomingBeam::kSearchBeatCount)
                {
                    beam.judgmentLocked = true;
                    if (!beam.lockSoundPlayed)
                    {
                        sound.Play(SOUND_BEAM_DECITION); // カン
                        beam.lockSoundPlayed = true;
                    }
                }

                // ロックされるまでは通常通りプレイヤーを追従
                if (!beam.judgmentLocked)
                {
                    beam.lockedSide = playerSide;
                    beam.lockedPos = playerBasePos;
                    beam.lockedPos.x += playerSide * playerMoveRange;
                }

                if (musicTime >= beam.lockTime)
                {
                    if (!beam.fireSoundPlayed)
                    {
                        sound.Play(SOUND_BEAM_FIRING);
                        beam.fireSoundPlayed = true;
                        beam.fireBeam = true;
                    }
                    
                    beam.state = BeamState::Fired;
                    //0.5sごとに判定を行うための時間セット
                    beam.nextCountTime = musicTime + HomingBeam::kCountInterval;
                }
            }
            else if (beam.state == BeamState::Fired)
            {
                if (beam.fireBeam)
                {

                }
                if (musicTime >= beam.hitTime)
                {
                    beam.state = BeamState::Resolved;
                }
                else
                {
                    if (musicTime >= beam.nextCountTime && beam.nextCountTime < beam.hitTime)
                    {
                        bool dodged = (playerSide != beam.lockedSide);
                        if (dodged) perfect++;
                        else miss++;
                        beam.nextCountTime += HomingBeam::kCountInterval;
                    }
                }
            }
        }
    }
    //ビームの状態遷移ロジック
 //   if(bgmstate == BGMstate::play)
 //   {
 //       for (auto& beam : homingBeams)
 //       {
 //           if(!beam.chargeSoundPlayerd && musicTime >= beam.chargeTime)
 //           {
 //               sound.Play(SOUND_BEAM_SARCH);   //探してる時の効果音
 //               beam.chargeSoundPlayerd = true;
	//		}

 //           if (beam.state == BeamState::Homing)
 //           {
 //               //早期ロック地点をまだ迎えてなかったら通常通り追従
 //               if (!beam.judgmentLocked)
 //               {
 //                   float earlyLockTime = beam.chargeTime + (beam.lockTime - beam.chargeTime) * HomingBeam::kEarlyLockRatio;

 //                   beam.lockedSide = playerSide;
 //                   beam.lockedPos = playerBasePos;
 //                   beam.lockedPos.x += playerSide * playerMoveRange;

 //                   if (musicTime >= earlyLockTime)
 //                   {
 //                       beam.judgmentLocked = true; // これ以降、lockedSide/lockedPosは固定される

 //                       if (!beam.lockSoundPlayed)
 //                       {
 //                           sound.Play(SOUND_BEAM_DECITION);    //打つ場所が確定した時の効果音
 //                           beam.lockSoundPlayed = true;
 //                       }
 //                   }
 //               }

 //               if (musicTime >= beam.lockTime)
 //               {
 //                   if (!beam.fireSoundPlayed)
 //                   {
 //                       sound.Play(SOUND_BEAM_FIRING);
 //                       beam.fireSoundPlayed = true;
 //                   }
 //                   beam.state = BeamState::Fired;
 //               }
 //           }
 //           else if (beam.state == BeamState::Fired)
 //           {
 //               if (musicTime >= beam.hitTime)
 //               {
	//				bool dodged = (playerSide != beam.lockedSide);
 //                   if (dodged) success++;  //判定変えたかったら変えてね
	//				else miss++;
 //                   beam.state = BeamState::Resolved;
 //               }
 //           }
 //       }
	//}

    //ミス判定
    if (bgmstate == BGMstate::play)
    {
        for (int i = (int)notes.size() - 1; i >= 0; i--)
        {
            if (notes[i].note == FLICK_RIGHT || notes[i].note == FLICK_LEFT) continue;
            if (notes[i].note == LONGNOTE) continue;
            if (musicTime - notes[i].time > kGud)
            {
                notes.erase(notes.begin() + i);
                miss++;
            }
        }
    }


    for (int i = 0; i < 6; i++)
    {
        if (m_judggeEffects[i].timer > 0.0f)
        {
            m_judggeEffects[i].timer -= elapsedTime;
        }
    }

    // ─ 再生中は音楽時間を進める / 停止中はエディタ位置を更新 ────────
    if (bgmstate == BGMstate::play)
    {
        if (highSpeed < 0.0f) highSpeed = 0.0f;
        musicTime += elapsedTime * highSpeed; // ハイスピード倍率をかけて進む

        for (const auto& Ik : kLaneKeys)
            JudggeLane(Ik.key, Ik.laneIndex);

        JudggeFlickNotes();
		JudggeLongNoteHold();   //ロングノーツの判定
    }
    //else
    //{
    //    // 停止中：マウスホイール または ↑↓ キーで拍位置を移動
    //    float wheel = ImGui::GetIO().MouseWheel;
    //    editorBeat += wheel;
    //    if (GetAsyncKeyState(VK_UP) & 0x8000) editorBeat += 0.1f;
    //    if (GetAsyncKeyState(VK_DOWN) & 0x8000) editorBeat -= 0.1f;
    //}
    KeySpacePrev = keySpace;
}
#pragma endregion

#pragma region Frame Render

// ─────────────────────────────────────────────
// Render : 毎フレームの描画処理
//
//   描画順序:
//     1. Clear（背景クリア）
//     2. ビュー・プロジェクション行列の更新
//     3. 左パネル（ImGui）：ノーツ操作 UI ＋ 3Dプレビュー
//     4. DirectX PrimitiveBatch：グリッド・パス・ノーツのクワッド
//     5. 3Dモデル描画（noteModel があれば）
//     6. 下部バー（ImGui）：タイムライン・再生操作
//     7. ImGui レンダリング（Draw Dataを DX11 に渡す）
// ─────────────────────────────────────────────
void Game::Render()
{
    // 最初の1フレームは描画しない（リソース初期化が完了していないため）
    if (m_timer.GetFrameCount() == 0) return;

    Clear();

    m_deviceResources->PIXBeginEvent(L"Render");
    auto context = m_deviceResources->GetD3DDeviceContext();

    // ── レイアウト計算 ─────────────────────────
    float screenW = ImGui::GetIO().DisplaySize.x;
    float screenH = ImGui::GetIO().DisplaySize.y;
    float leftW = 250.0f;   // 左パネルの幅
    float bottomH = 60.0f;    // 下バーの高さ
    float chartW = screenW - leftW;   // 3Dビューの幅
    float chartH = screenH - bottomH; // 3Dビューの高さ

    // ── View / Projection 行列の設定 ─────────────
    float aspect = chartW / chartH;

    m_camAspect = chartW / chartH;
    m_view = DirectX::SimpleMath::Matrix(
        DirectX::XMMatrixLookAtLH(
            DirectX::XMLoadFloat3(&m_camPos),
            DirectX::XMLoadFloat3(&m_camLook),
            DirectX::XMLoadFloat3(&m_camUp)
        )
    );
    m_proj = DirectX::SimpleMath::Matrix::CreatePerspectiveFieldOfView(
        m_camFovy, m_camAspect, m_camNear, m_camFar
    );
    if (ViewMode)
    {
        // サイドビュー：斜めからステージ全体を俯瞰
        m_view = Matrix::CreateLookAt(
            Vector3(cameraPos.x, cameraPos.y, cameraPos.z),
            Vector3(0.0f, -55.0f, -200.0f),
            Vector3::UnitY
        );
        //    // デフォルトビュー：真上から真下（Z+ 方向）を見下ろす
        //m_view = Matrix::CreateLookAt(
        //    Vector3(cameraPos.x, cameraPos.y, cameraPos.z),
        //    Vector3(cameraPos.x, -55.0f, cameraPos.z + 1.0f),
        //    Vector3::UnitZ
        //);
    }
    //else
    //{
    //    // デフォルトビュー：真上から真下（Z+ 方向）を見下ろす
    //    m_view = Matrix::CreateLookAt(
    //        Vector3(cameraPos.x, cameraPos.y, cameraPos.z),
    //        Vector3(cameraPos.x, -55.0f, cameraPos.z + 1.0f),
    //        Vector3::UnitZ
    //    );
    //}
  

    m_proj = Matrix::CreatePerspectiveFieldOfView(
        XMConvertToRadians(60.0f), // 水平FOV 60度
        aspect,
        1.0f,    // Near クリップ
        5000.0f  // Far クリップ
    );

    // ── マウスが編集エリア外かどうかを毎フレーム判定 ─
    {
        ImVec2 mp = ImGui::GetMousePos();
        isOutRange = !(mp.x >= leftW && mp.x <= leftW + chartW &&
            mp.y >= 0.0f && mp.y <= chartH);
    }
    // ════════════════════════════════════════════
    // デバッグ情報ウィンドウ
    // ════════════════════════════════════════════
    {
        ImGui::Begin("Debug Info");

        ImGui::Text("camPos:  %.1f, %.1f, %.1f", m_camPos.x, m_camPos.y, m_camPos.z);
        ImGui::Text("camLook: %.1f, %.1f, %.1f", m_camLook.x, m_camLook.y, m_camLook.z);
        ImGui::Text("camState: %d", m_camState);
        ImGui::Text("worldPos: %.1f, %.1f, %.1f", worldPos.x, worldPos.y, worldPos.z);
        ImGui::Text("rayDir: %.3f, %.3f, %.3f", rayDir.x, rayDir.y, rayDir.z);

        ImGui::End();
    }
    // ════════════════════════════════════════════
    // 判定表示ウィンドウ
    // ════════════════════════════════════════════
    {
        ImGui::Begin("Judgge");

        ImGui::Text("PERFECT:  %.1i", perfect);
        ImGui::Text("GREAT: %.1i", great);
        ImGui::Text("GOOD: %i", good);
        ImGui::Text("MISS: %.1i", miss);

        ImGui::End();
    }
    // ════════════════════════════════════════════
    // 左パネル UI
    // ════════════════════════════════════════════
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(leftW, screenH - bottomH));
        ImGui::Begin("LeftPanel", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        // ハイスピード倍率スライダー（ノーツの移動速度）
        ImGui::SliderFloat("HighSpeed", &highSpeed, 0.1f, 5.0f);
        if (m_EditorState == EditorState::PLACING_SPAWN || m_EditorState == EditorState::PLACING_JUDGE)
        {
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 1.0f, 1.0f),
                "Placement Height: %.1f (Shift+Wheel to adjust)", m_PlacementHeight);
        }
        ImGui::SliderFloat("Approach Beats (HS-like)", &g_approachBeats, 0.5f, 8.0f);
        ImGui::SliderFloat("Min Travel Time (sec)", &g_minTravelTime, 0.1f, 1.0f);
        ImGui::TextDisabled("New notes: travelTime = approachBeats * (60/bpm)  [BPM=%.0f]", bpm);

        // ── ノーツ種別選択ボタン ─────────────────
        const char* noteNames[] = { "TAP", "LONG","FLICK_RIGHT", "FLICK_LEFT" };
        NoteType noteTypes[] = { TAP, LONGNOTE, FLICK_RIGHT, FLICK_LEFT };

        // カメラビュー切替ボタン
        if (ImGui::Button("Play View", ImVec2(-1, 0))) {
            ViewMode = true;
            cameraPos = { 0.0f, 0.0f, -500.0f };
        }
        if (ImGui::Button("Default View", ImVec2(-1, 0))) {
            ViewMode = false;
        }

        // 各ノーツ種別ボタン（選択中は青くハイライト）
        for (int i = 0; i < (int)std::size(noteTypes); i++)
        {
            bool selected = (currentNoteType == noteTypes[i]);
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
            if (ImGui::Button(noteNames[i], ImVec2(-1, 0)))
                currentNoteType = noteTypes[i];
            if (selected)
                ImGui::PopStyleColor();
        }

        // ── レーン（キー）選択ボタン ─────────────
        ImGui::SeparatorText("Key");
        const char* keyNames[] = { "S","D","F","J","K","L" };
        for (int i = 0; i < 6; i++)
        {
            if (i > 0) ImGui::SameLine();
            bool sel = (m_TempNote.lane == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            char label[4];
            sprintf_s(label, "%s", keyNames[i]);
            if (ImGui::Button(label, ImVec2(28, 0))) m_TempNote.lane = i;
            if (sel) ImGui::PopStyleColor();
        }

		//フリック専用：終点レーン選択------------------------
        if(currentNoteType == FLICK_RIGHT || currentNoteType == FLICK_LEFT)
        {
            ImGui::SeparatorText("To (Flick End Lane)");
            for (int i = 0; i < 6; i++)
            {
				if (i > 0) ImGui::SameLine();   // 横並び
                bool sel = (m_TempNote.flickLaneTo == i);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.6f, 1.0f));
                char label[16];
                sprintf_s(label, "%s##to%d", keyNames[i], i);
                if (ImGui::Button(label, ImVec2(28, 0))) m_TempNote.flickLaneTo = i;
                if (sel) ImGui::PopStyleColor();
            }
            if(m_TempNote.flickLaneTo < 0)
				ImGui::TextDisabled("未設定 = Fromと同じ扱い");
		}

        //ロング
        if(currentNoteType == LONGNOTE)
        {
            ImGui::SeparatorText("Hold Duration");
            static float longDurationInput = 1.0f;
            //ImGui::InputFloat("Duration (sec)##long", &longDurationInput, 0.1f);
            if (longDurationInput < 0.05f) longDurationInput = 0.05f;
            m_TempNote.holdDuration = longDurationInput;
		}

        // ── ノーツ配置ボタン ─────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Place");

        bool isPlacing = (m_EditorState == EditorState::PLACING_SPAWN ||
            m_EditorState == EditorState::PLACING_JUDGE);

        if (isPlacing)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));

        // ボタンラベルはエディタ状態に応じて変化
        if (ImGui::Button(
            m_EditorState == EditorState::PLACING_SPAWN ? "Click: Spawn Pos" :
            m_EditorState == EditorState::PLACING_JUDGE ? "Click: Judge Pos" :
            "Place Note", ImVec2(-1, 0)))
        {
            if (!isPlacing)
            {
				int savedLane = m_TempNote.lane; // 現在のレーンを保存
				int savedFlickLaneTo = m_TempNote.flickLaneTo; // 現在のフリック終点レーンを保存
				float savedHoldDuration = m_TempNote.holdDuration; // 現在のロングノーツの保持時間を保存
                // 配置開始：テンプレートノーツを初期化
                m_TempNote = Note();
                m_TempNote.note = currentNoteType;
				m_TempNote.lane = savedLane; // 保存したレーンを復元
				m_TempNote.flickLaneTo = savedFlickLaneTo; // 保存したフリック終点レーンを復元
				m_TempNote.holdDuration = savedHoldDuration; // 保存したロングノーツの保持時間を復元

                m_PlacementHeight = 150.0f;

                m_EditorState = EditorState::PLACING_SPAWN;
            }
            else
            {
                m_EditorState = EditorState::IDLE; // 配置中断
            }
        }
        if (isPlacing) ImGui::PopStyleColor();

        // 状態ガイドテキスト
        switch (m_EditorState)
        {
        case EditorState::PLACING_SPAWN:
            ImGui::TextColored(ImVec4(0, 1, 0.5f, 1), "1. Click spawn position");  break;
        case EditorState::PLACING_JUDGE:
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "2. Click judge position");  break;
        case EditorState::EDITING:
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Editing selected note");        break;
        default:
            ImGui::TextDisabled("Click note to select");                             break;
        }

        // ── セーブ / ロード ─────────────────────
        ImGui::SeparatorText("Fire");
        ImGui::InputText("##filename", filename, 64);
        if (ImGui::Button("Save", ImVec2(90, 0)))
        {
            if (mirrorMode) mirrorMode = false;
            SaveChart(filename);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load", ImVec2(90, 0)))
            LoadChart(filename);

        //判定円を動かすところ─────────────────────
        ImGui::SeparatorText("Judgge Circles");

        const char* laneNames[] = { "S","D","F","J","K","L" };

        for (int i = 0; i < (int)judggeCircles.size(); i++)
        {
            auto& circle = judggeCircles[i];

            if (ImGui::TreeNode(laneNames[i]))
            {
                //キーフレーム一覧
                for (int k = 0; k < (int)circle.keyframe.size(); k++)
                {
                    auto& key = circle.keyframe[k];
                    ImGui::PushID(k);
                    ImGui::SliderFloat("time", &key.time, 0.0f,255.0f);
                    ImGui::SliderFloat("x", &key.x, -255.0f,255.0f);
                    ImGui::SliderFloat("z", &key.z, 255.0f,-255.0f);
                    ImGui::SameLine();
                    if (ImGui::Button("x")) //削除
                    {
                        circle.keyframe.erase(circle.keyframe.begin() + k);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }

                static float newKeyTime = 0.0f;
                static float newKeyX = 0.0f;
                static float newKeyY = -55.0f;
                static float newKeyZ = -210.0f;

                ImGui::InputFloat("Time to add", &newKeyTime, 0.1f);
                ImGui::InputFloat("X to add", &newKeyX, 1.0f);
                ImGui::InputFloat("Z to add", &newKeyZ, 1.0f);

                if (ImGui::Button("+ Add KeyFrame"))
                {
                    circle.keyframe.push_back({ newKeyTime, newKeyX, 0.0f, newKeyZ });

                    // 時刻順にソート
                    std::sort(circle.keyframe.begin(), circle.keyframe.end(),
                        [](const JudggeCircleframe& a, const JudggeCircleframe& b) {
                            return a.time < b.time;
                        });
                }
                ImGui::TreePop();
            }
        }


        //ビーム描画
		ImGui::SeparatorText("Homing Beams");
        for(int i = 0; i <(int)homingBeams.size(); i++)
        {
			ImGui::PushID(i);   // ユニークIDをプッシュ
			auto& b = homingBeams[i];
            const char* stateStr =
                b.state == BeamState::Homing ? "Homing" :
                b.state == BeamState::Fired ? "Fired" : "Resolved";
            ImGui::Text("Beam %d [%s]", i, stateStr);
            ImGui::SliderFloat("charge", &b.chargeTime, 0.0f, 60.0f);
            ImGui::SliderFloat("lock", &b.lockTime, 0.0f, 60.0f);
            ImGui::SliderFloat("hit", &b.hitTime, 0.0f, 60.0f);
            if (ImGui::Button("Delete"))
            {
                homingBeams.erase(homingBeams.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
		}

        ImGui::Spacing();
		ImGui::InputFloat("New Charge", &m_newBeamChargeTime, 0.01f);
        ImGui::InputFloat("New Lock", &m_newBeamLockTime, 0.01f);
        ImGui::InputFloat("New Hit", &m_newBeamHitTime, 0.01f);
        if (ImGui::Button("+ Add Beam", ImVec2(-1, 0)))
        {
            HomingBeam b;
            b.chargeTime = m_newBeamChargeTime;
            b.lockTime = m_newBeamLockTime;
            b.hitTime = m_newBeamHitTime;
            InitBeamScatter(b); //ビームの周りにあるビーム
            homingBeams.push_back(b);

            std::sort(homingBeams.begin(), homingBeams.end(),
                [](const HomingBeam& a, const HomingBeam& c) { return a.lockTime < c.lockTime; });
        }

        // ── 選択中ノーツのプロパティ編集 ──────────
        ImGui::SeparatorText("Selected Note");
        if (selectedNoteIndex >= 0 && selectedNoteIndex < (int)notes.size())
        {
            Note& n = notes[selectedNoteIndex];

            // レーン変更ボタン
            ImGui::Text("Key:");
            ImGui::SameLine();
            for (int i = 0; i < 6; i++)
            {
                if (i > 0) ImGui::SameLine();
                bool sel = (n.lane == i);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                char label[8];
                sprintf_s(label, "%s##k%d", keyNames[i], i);
                if (ImGui::Button(label, ImVec2(24, 0))) n.lane = i;
                if (sel) ImGui::PopStyleColor();
            }

            // 時刻編集（SpawnTime = 出現時刻、ArriveTime = 判定時刻）
   //         ImGui::InputFloat("StartTime", &n.spawnTime, 0.01f);
   //         ImGui::InputFloat("ArriveTime", &n.time, 0.01f);
   //         if (n.time <= n.GetSpawnTime())
   //             n.time = n.GetSpawnTime() + 0.01f; // 整合性チェック
   //         ImGui::Text("TravelTime: %.2f sec", n.time - n.GetSpawnTime());
			//ImGui::Text("From %.2f to %.2f sec", n.GetSpawnTime(), n.time);

            //時刻編集（3段階）
            //1.表示開始時刻 = SpawnTime(クリック位置から表示開始時間)
            //2.到達時刻 = arrivalTime(判定ラインに到達する時刻)
            //3.終了時刻 = time(通り過ぎて消える)
            ImGui::SeparatorText("Timeline Settings");
			float prevSpawnTime = n.spawnTime;  //編集前の値を保持

            if (ImGui::InputFloat("Display Start Time (sec)", &n.spawnTime, 0.01f))
            {
				float delta = n.spawnTime - prevSpawnTime; //変更量
				n.arrivalTime += delta; //到達時刻も同じだけ変更
				n.time += delta; //終了時刻も同じだけ変更
            }
			ImGui::InputFloat("Arrival Time (sec)", &n.arrivalTime, 0.01f);
			ImGui::InputFloat("End Time (sec)", &n.time, 0.01f);
            // Selected Note パネル内、Timeline Settings セクションの末尾あたりに追加
            if (ImGui::Button("Recalc from BPM (Approach Beats)"))
            {
                float secondsPerBeat = (bpm > 0.0f) ? 60.0f / bpm : 0.5f;
                float travelTime = g_approachBeats * secondsPerBeat;
                if (travelTime < g_minTravelTime) travelTime = g_minTravelTime;

                if (n.note == LONGNOTE)
                {
                    n.arrivalTime = n.GetSpawnTime() + travelTime;
                    if (n.time <= n.arrivalTime) n.time = n.arrivalTime + 0.1f; // holdDurationは維持されないので必要なら別途調整
                }
                else
                {
                    n.time = n.GetSpawnTime() + travelTime;
                }
            }

            //整合性チェック
            if(n.arrivalTime <= n.GetSpawnTime())
				n.arrivalTime = n.GetSpawnTime() + 0.1f;    //出現から到達までの時間を絶対に+0.1s以上に設定する。これ以下にすると高速で飛んでいくバグが起きる（バグというかそれ以下の時間で到達するには高速で飛ばないと行けないからそう見える）
            if(n.time <= n.arrivalTime)
				n.time = n.arrivalTime + 0.01f;

			ImGui::Text("Display: %.2f sec, Arrival: %.2f sec, End: %.2f sec", n.arrivalTime - n.GetSpawnTime());
			ImGui::Text("Pass through: %.2f sec", n.time - n.arrivalTime);

            ImGui::SeparatorText("Edit Timeline");
			float fromTime = n.GetSpawnTime();
			float toTime = n.time;
            if(ImGui::InputFloat("From (sec)##from", &fromTime, 0.01f,0.1f))
            {
                if (fromTime < toTime) n.spawnTime = fromTime;
			}
            if (ImGui::InputFloat("To (sec)##to", &toTime, 0.01f, 0.1f))
            {
                if (toTime > n.GetSpawnTime()) n.time = toTime;
            }

            ImGui::Spacing();

            // Spawn 位置・Judge 位置の数値入力
            ImGui::SeparatorText("Spawn Pos");
            float sp[3] = { n.spawnPos.x, n.spawnPos.y, n.spawnPos.z };
            if (ImGui::InputFloat3("##sp", sp)) n.spawnPos = { sp[0], sp[1], sp[2] };

            ImGui::SeparatorText("Judge Pos");
            float jp[3] = { n.judgePos.x, n.judgePos.y, n.judgePos.z };
            if (ImGui::InputFloat3("##jp", jp)) n.judgePos = { jp[0], jp[1], jp[2] };

            // 制御点（ベジェ曲線のコントロールポイント）編集
            ImGui::SeparatorText("Control Points");
            ImGui::TextDisabled("0=line  1=quad  2=cubic");
            for (int i = 0; i < (int)n.controlPoints.size(); i++)
            {
                char label[32];
                sprintf_s(label, "P%d", i);
                float v[3] = { n.controlPoints[i].x, n.controlPoints[i].y, n.controlPoints[i].z };
                if (ImGui::InputFloat3(label, v))
                    n.controlPoints[i] = { v[0], v[1], v[2] };
                ImGui::SameLine();
                char del[16];
                sprintf_s(del, "x##%d", i);
                if (ImGui::Button(del)) // 制御点を削除
                {
                    n.controlPoints.erase(n.controlPoints.begin() + i);
                    break;
                }
            }
            // 制御点の追加（midpoint に自動配置）
            if (ImGui::Button("+ Add Point", ImVec2(-1, 0)))
            {
                DirectX::SimpleMath::Vector3 mid;
                mid.x = (n.spawnPos.x + n.judgePos.x) * 0.5f;
                mid.y = (n.spawnPos.y + n.judgePos.y) * 0.5f;
                mid.z = (n.spawnPos.z + n.judgePos.z) * 0.5f;
                n.controlPoints.push_back(mid);
            }

            if (n.note == FLICK_RIGHT || n.note == FLICK_LEFT)
            {
                ImGui::Text("To:");
                ImGui::SameLine();
                for (int i = 0; i < 6; i++)
                {
                    if (i > 0) ImGui::SameLine();
                    bool sel = (n.flickLaneTo == i);
					if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.6f, 1.0f));    // 緑色
                    char label[8];
                    sprintf_s(label, "%s##nto%d", keyNames[i], i);
                    if (ImGui::Button(label, ImVec2(24, 0))) n.flickLaneTo = i;
                    if (sel) ImGui::PopStyleColor();
                }
            }

            ImGui::Spacing();
            // 削除ボタン（赤色）
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Delete Note", ImVec2(-1, 0)))
            {
                notes.erase(notes.begin() + selectedNoteIndex);
                selectedNoteIndex = -1;
                m_EditorState = EditorState::IDLE;
            }
            ImGui::PopStyleColor();
        }

        // ════════════════════════════════════════
        // 3D Path Preview（左パネル下部のミニビュー）
        //
        //   全ノーツのベジェパスを等角投影で描画する。
        //   現在の再生位置にいるノーツは移動中の丸マーカーを表示。
        // ════════════════════════════════════════
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            m_EditorScreenPos = pos;
            m_ChartW = chartW;
            m_ChartH = chartH;
            chartPosX = pos.x;

            bpm = 120;
            float secondPerBeat = 60.0f / bpm;
            currentBeat = (bgmstate == BGMstate::play)
                ? musicTime / secondPerBeat
                : editorBeat;

            // プレビュー矩形の範囲
            float previewW = leftW - 20.0f;
            float previewH = 200.0f;
            ImVec2 previewMin(pos.x, pos.y);
            ImVec2 previewMax(previewMin.x + previewW, previewMin.y + previewH);
            ImVec2 previewCenter(
                (previewMin.x + previewMax.x) * 0.5f,
                previewMin.y + previewH * 0.62f); // 縦の中心をやや上に設定

            m_PreviewMin = previewMin;
            m_PreviewMax = previewMax;
            m_PreviewCenter = previewCenter;
            m_PreviewValid = true;

            // 背景・枠・タイトル
            draw->AddRectFilled(previewMin, previewMax, IM_COL32(18, 20, 26, 220));
            draw->AddRect(previewMin, previewMax, IM_COL32(130, 150, 180, 180));
            draw->AddText(ImVec2(previewMin.x + 8.0f, previewMin.y + 6.0f),
                IM_COL32(220, 230, 255, 230), "3D Path Preview");

            ImGui::Dummy(ImVec2(previewW, previewH + 8.0f)); // ImGui レイアウト用の空領域

            // ─ 等角投影関数（ワールド座標 → プレビュー画面座標）
            auto Project3D = [&](const DirectX::SimpleMath::Vector3& v) -> ImVec2
                {
                    float sx = previewCenter.x + v.x * kPreviewScaleX + v.z * kPreviewScaleXZ;
                    float sy = previewCenter.y - v.y * kPreviewScaleY - v.z * kPreviewScaleYZ;
                    return ImVec2(sx, sy);
                };

            // ─ 座標軸の描画（X: 赤, Z: 青, Y: 緑）
            {
                ImVec2 origin = Project3D({ 0.0f, -55.0f, -210.0f });
                draw->AddLine(origin, Project3D({ 360.0f, -55.0f, -210.0f }), IM_COL32(255, 90, 90, 180), 1.5f);
                draw->AddLine(origin, Project3D({ 0.0f, -55.0f, 900.0f }), IM_COL32(120, 170, 255, 180), 1.5f);
                draw->AddLine(origin, Project3D({ 0.0f, 120.0f, -210.0f }), IM_COL32(120, 255, 150, 180), 1.5f);
                draw->AddText(Project3D({ 380.0f, -55.0f, -210.0f }), IM_COL32(255, 130, 130, 220), "X");
                draw->AddText(Project3D({ 0.0f, -55.0f, 940.0f }), IM_COL32(150, 190, 255, 220), "Z");
                draw->AddText(Project3D({ 0.0f, 135.0f, -210.0f }), IM_COL32(150, 255, 180, 220), "Y");
            }

            // ─ 各ノーツのパス・マーカー・移動中丸の描画 ──────────
            float previewTime = (bgmstate == BGMstate::play)
                ? musicTime
                : editorBeat * secondPerBeat;

            for (int i = 0; i < (int)notes.size(); i++)
            {
                const auto& note = notes[i];
                bool isSelected = (i == selectedNoteIndex);

                // 選択中は黄色、それ以外は水色
                ImU32 pathCol = isSelected
                    ? IM_COL32(255, 230, 80, 230)
                    : IM_COL32(90, 190, 255, 150);

                // パスを 24 分割して折れ線で描画
                float startT = note.GetSpawnTime();
                float endT = note.time;
                float rangeT = endT - startT;
                ImVec2 prev = Project3D(note.spawnPos);
                for (int s = 1; s <= 24; s++)
                {
                    float sampleT = startT + rangeT * (float)s / 24.0f;
                    ImVec2 cur = Project3D(EvaluateNotePosition(note, sampleT));
                    draw->AddLine(prev, cur, pathCol, isSelected ? 2.0f : 1.0f);
                    prev = cur;
                }

                // spawnPos = 青い円、judgePos = 赤い四角
                ImVec2 spawnP = Project3D(note.spawnPos);
                ImVec2 judgeP = Project3D(note.judgePos);
                draw->AddCircleFilled(spawnP, 3.5f, IM_COL32(0, 210, 255, 210));
                draw->AddRectFilled(
                    ImVec2(judgeP.x - 4.0f, judgeP.y - 4.0f),
                    ImVec2(judgeP.x + 4.0f, judgeP.y + 4.0f),
                    IM_COL32(255, 110, 110, 230));

                // 現在時刻にいるノーツには移動中マーカー（大きめの丸）を表示
                if (previewTime >= startT && previewTime <= endT)
                {
                    ImVec2 bullet = Project3D(EvaluateNotePosition(note, previewTime));
                    draw->AddCircleFilled(bullet,
                        isSelected ? 7.0f : 5.0f,
                        isSelected ? IM_COL32(255, 255, 80, 255) : IM_COL32(120, 255, 170, 230));
                }
            }
        }
        ImGui::End();
    }

    // ════════════════════════════════════════════
    // DirectX PrimitiveBatch による 3D 描画
    //   ・グリッドライン
    //   ・ノーツのパス（折れ線）
    //   ・Spawn/Judge マーカー（十字線）
    //   ・移動中ノーツの現在位置マーカー（水色十字）
    //   ・ノーツ本体（カラーつきクワッド）
    // ════════════════════════════════════════════
    context->OMSetBlendState(m_states->Opaque(), nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(m_states->DepthDefault(), 0);
    context->RSSetState(m_states->CullNone());

    m_basicEffect->SetView(m_view);
    m_basicEffect->SetProjection(m_proj);
    m_basicEffect->Apply(context);
    context->IASetInputLayout(m_inputLayout.Get());

    m_primitiveBatch->Begin();

    //y座標が現在どこに設定されてるかをわかりやすくする赤い線　Shift+マウスホイールで上下移動可能
    m_primitiveBatch->DrawLine(
        VertexPositionColor(Vector3(0.0f, m_PlacementHeight, 0.0f), Colors::Red),
        VertexPositionColor(Vector3(0.0f, m_PlacementHeight, 2000.0f), Colors::Red)
    );

    // 判定ライン（z=-210 赤いライン）
    m_primitiveBatch->DrawLine(
        VertexPositionColor(Vector3(-300.0f, -55.0f, -210.0f), Colors::Red),
        VertexPositionColor(Vector3(300.0f, -55.0f, -210.0f), Colors::Red)
    );

    // z=0 の白い基準ライン
    m_primitiveBatch->DrawLine(
        VertexPositionColor(Vector3(-400.0f, -55.0f, 0.0f), Colors::White),
        VertexPositionColor(Vector3(400.0f, -55.0f, 0.0f), Colors::White)
    );

    // グリッドライン（z=-200 ～ z=1000、100単位ごと）
    for (float z = -200.0f; z <= 1000.0f; z += 100.0f)
    {
        XMVECTORF32 col = (fabs(z) < 0.1f) ? Colors::White : Colors::Gray;
        m_primitiveBatch->DrawLine(
            VertexPositionColor(Vector3(-400.0f, -55.0f, z), col),
            VertexPositionColor(Vector3(400.0f, -55.0f, z), col)
        );
    }

    // 各ノーツのパス・マーカー描画 
    for (int i = 0; i < (int)notes.size(); i++)
    {
        auto& note = notes[i];
        bool  isSel = (i == selectedNoteIndex);

        XMVECTORF32 pathCol = isSel ? Colors::Yellow : Colors::Cyan;
        XMVECTORF32 spawnCol = isSel ? Colors::Yellow : Colors::LimeGreen;
        XMVECTORF32 judgeCol = isSel ? Colors::Orange : Colors::OrangeRed;

        // パスを 24 分割して折れ線（0→1 均等サンプル）
        Vector3 prev = note.spawnPos;
        for (int s = 1; s <= 24; s++)
        {
            float t = (float)s / 24.0f;
            Vector3 cur;
            if (note.controlPoints.empty())
                cur = LerpVector3(note.spawnPos, note.judgePos, t);
            else if (note.controlPoints.size() == 1)
            {
                const auto& p1 = note.controlPoints[0];
                cur = LerpVector3(LerpVector3(note.spawnPos, p1, t),
                    LerpVector3(p1, note.judgePos, t), t);
            }
            else
            {
                const auto& p1 = note.controlPoints[0];
                const auto& p2 = note.controlPoints[1];
                auto a = LerpVector3(note.spawnPos, p1, t);
                auto b = LerpVector3(p1, p2, t);
                auto c = LerpVector3(p2, note.judgePos, t);
                cur = LerpVector3(LerpVector3(a, b, t), LerpVector3(b, c, t), t);
            }
            m_primitiveBatch->DrawLine(
                VertexPositionColor(prev, pathCol),
                VertexPositionColor(cur, pathCol)
            );
            prev = cur;
        }

        // Spawn マーカー（小さい十字）
        {
            float ms = 15.0f;
            m_primitiveBatch->DrawLine(
                VertexPositionColor(note.spawnPos + Vector3(-ms, 0, 0), spawnCol),
                VertexPositionColor(note.spawnPos + Vector3(ms, 0, 0), spawnCol));
            m_primitiveBatch->DrawLine(
                VertexPositionColor(note.spawnPos + Vector3(0, 0, -ms), spawnCol),
                VertexPositionColor(note.spawnPos + Vector3(0, 0, ms), spawnCol));
        }
        // Judge マーカー（大きい十字）
        {
            float mj = 20.0f;
            m_primitiveBatch->DrawLine(
                VertexPositionColor(note.judgePos + Vector3(-mj, 0, 0), judgeCol),
                VertexPositionColor(note.judgePos + Vector3(mj, 0, 0), judgeCol));
            m_primitiveBatch->DrawLine(
                VertexPositionColor(note.judgePos + Vector3(0, 0, -mj), judgeCol),
                VertexPositionColor(note.judgePos + Vector3(0, 0, mj), judgeCol));
        }

        // 再生中：現在位置に水色の十字マーカーを表示
        float st = note.GetSpawnTime();
        float et = note.time;
        if (bgmstate == BGMstate::play && musicTime >= st && musicTime <= et)
        {
            Vector3 nowPos = EvaluateNotePosition(note, musicTime);
            float   ns = 12.0f;
            m_primitiveBatch->DrawLine(
                VertexPositionColor(nowPos + Vector3(-ns, 0, 0), Colors::Aqua),
                VertexPositionColor(nowPos + Vector3(ns, 0, 0), Colors::Aqua));
            m_primitiveBatch->DrawLine(
                VertexPositionColor(nowPos + Vector3(0, -ns, 0), Colors::Aqua),
                VertexPositionColor(nowPos + Vector3(0, ns, 0), Colors::Aqua));
        }
    }

    //ビーム描画
    context->OMSetBlendState(m_states->Additive(), nullptr, 0xFFFFFFFF);
    Vector3 beamOrigin = enemyPos + Vector3(0.0f, 50.0f, 0.0f);

    for (auto& beam : homingBeams)
    {
        if (beam.state == BeamState::Resolved) continue;
        if (musicTime < beam.chargeTime) continue;

        //Vector3 lockTarget = playerBasePos + Vector3(playerSide * playerMoveRange, 0, 0);
        Vector3 lockTarget = beam.judgmentLocked
            ? beam.lockedPos                                                    //ロック済みならそこへ収束
            : (playerBasePos + Vector3(playerVisualX, 0, 0));    //まだならプレイヤーを追従

        if (beam.state == BeamState::Homing)
        {
            // ── 収束進行度 t(0=バラバラ, 1=収束完了) ──
            float total = beam.lockTime - beam.chargeTime;
            if (total < 0.01f) total = 0.01f;
            float t = std::clamp((musicTime - beam.chargeTime) / total, 0.0f, 1.0f);
            float easedT = t * t; // ease-inで後半に収束を加速

            // ── 散らばった候補線を描画。tが進むほどlockTargetへ寄っていく ──
            for (int i = 0; i < HomingBeam::kScatterCount; i++)
            {
                Vector3 scatterPos = lockTarget + beam.scatterOffsets[i];
                Vector3 to = LerpVector3(scatterPos, lockTarget, easedT);

                float thicknessFrom = 1.5f;
                float thicknessTo = 2.0f + easedT * 6.0f;

                XMVECTORF32 coreColor = { 1.0f, 0.9f, 0.6f, 0.9f };
                XMVECTORF32 glowColor = { 1.0f, 0.5f, 0.1f, 0.35f };

                DrawThickBeam(beamOrigin, to, thicknessFrom, thicknessTo, coreColor, glowColor);
            }
        }
        else // Fired
        {
            float thicknessFrom = 4.0f;
            float thicknessTo = 26.0f;

            XMVECTORF32 coreColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            XMVECTORF32 glowColor = { 1.0f, 0.15f, 0.15f, 0.6f };

            DrawThickBeam(beamOrigin, beam.lockedPos, thicknessFrom, thicknessTo, coreColor, glowColor);
        }
    }
    //ブレンドステートを元に戻す
    context->OMSetBlendState(m_states->Opaque(), nullptr, 0xFFFFFFFF);

    // ── ノーツ本体をカラーつきクワッドで描画 ────────
    //   再生中は EvaluateNotePosition で移動位置を計算し表示。
    //   停止中は spawnPos に固定表示する。
    float now = (bgmstate == BGMstate::play)
        ? musicTime
        : editorBeat * (60.0f / bpm);

    for (int i = 0; i < (int)notes.size(); i++)
    {
        const auto& note = notes[i];
        float st = note.GetSpawnTime();
        float et = note.time;

        DirectX::SimpleMath::Vector3 pos;
        if (bgmstate != BGMstate::play)
        {
            pos = note.spawnPos; // 停止中は出現位置に固定
        }
        else
        {
            if (now < st) continue; // SpawnTime 前は非表示
            if (now > et) continue; // JudgeTime 後は非表示
            pos = EvaluateNotePosition(note, now); // 補間位置を計算
        }

        // レーンごとに色を変える（S:赤 D:橙 F:黄 J:緑 K:青 L:紫）
        float hw = 40.0f; // ノーツ半幅（X方向）
        float hd = 10.0f; // ノーツ半奥行（Z方向）

        //XMVECTORF32 col;
        //switch (note.lane)
        //{
        //case 0: col = { 1.0f, 0.3f, 0.3f, 1.0f }; break; // 赤（S）
        //case 1: col = { 1.0f, 0.7f, 0.0f, 1.0f }; break; // 橙（D）
        //case 2: col = { 1.0f, 1.0f, 0.3f, 1.0f }; break; // 黄（F）
        //case 3: col = { 0.3f, 1.0f, 0.3f, 1.0f }; break; // 緑（J）
        //case 4: col = { 0.3f, 0.7f, 1.0f, 1.0f }; break; // 青（K）
        //case 5: col = { 0.7f, 0.3f, 1.0f, 1.0f }; break; // 紫（L）
        //default:col = { 1.0f, 1.0f, 1.0f, 1.0f }; break; // 白（不明）
        //}

        //// XZ 平面上の矩形（4頂点）
        //Vector3 v0 = pos + Vector3(-hw, 0, -hd);
        //Vector3 v1 = pos + Vector3(hw, 0, -hd);
        //Vector3 v2 = pos + Vector3(hw, 0, hd);
        //Vector3 v3 = pos + Vector3(-hw, 0, hd);

        //m_primitiveBatch->DrawQuad(
        //    VertexPositionColor(v0, col),
        //    VertexPositionColor(v1, col),
        //    VertexPositionColor(v2, col),
        //    VertexPositionColor(v3, col)
        //);
    }

    m_primitiveBatch->End();

    //判定円の描画
    {
        float now = (bgmstate == BGMstate::play)
            ? musicTime : editorBeat * (60.0f / bpm);

        auto spriteBatch = std::make_unique<DirectX::SpriteBatch>(context);
        spriteBatch->Begin();

        for (auto& circle : judggeCircles)
        {
            if (!circle.texture) continue;
            auto pos3D = circle.GetPosition(now);

            //3D座標をスクリーン座標に変換
            XMVECTOR worldVec = XMLoadFloat3(&pos3D);

            float screenW = ImGui::GetIO().DisplaySize.x;
            float screenH = ImGui::GetIO().DisplaySize.y;

            float size = 80.0f;
            RECT srcRect = { 0, 0, 128, 128 }; // 画像サイズに合わせる
            DirectX::XMFLOAT2 origin = { 64.0f, 64.0f };

            XMVECTOR screenVec = XMVector3Project(
                worldVec,
                0.0f, 0.0f,        // ビューポート左上
                screenW, screenH,   // ビューポートサイズ
                0.0f, 1.0f,        // 深度min/max
                m_proj,
                m_view,
                XMMatrixIdentity()
            );

            XMFLOAT3 screenPos;
            XMStoreFloat3(&screenPos, screenVec);

            // screenPos.z >= 1.0f はカメラ後方なのでスキップ
            if (screenPos.z >= 1.0f) continue;

            spriteBatch->Draw(
                circle.texture,     //sTextureセット
                DirectX::XMFLOAT2(screenPos.x, screenPos.y),    //ピクセル座標セット
                &srcRect,
                DirectX::Colors::White,
                0.0f,   //回転なし
                origin, //画像の中心を基準に
                size / 128.0f   //スケール
            );
        }

        for (auto& beam : homingBeams)
        {
            if (beam.state != BeamState::Homing) continue;
            if (musicTime < beam.chargeTime) continue;

            //float remain = beam.lockTime - musicTime;
            //float total = beam.lockTime - beam.chargeTime;
            //if (total < 0.01f) total = 0.01f;
            //float ratio = std::clamp(remain / total, 0.0f, 1.0f);

            Vector3 targetPos = playerBasePos;
            targetPos.x += playerSide * playerMoveRange;

            XMVECTOR worldVec = XMLoadFloat3(&targetPos);
            XMVECTOR screenVec = XMVector3Project(
                worldVec, 0.0f, 0.0f, screenW, screenH, 0.0f, 1.0f, m_proj, m_view, XMMatrixIdentity());
            XMFLOAT3 screenPos;
            XMStoreFloat3(&screenPos, screenVec);
            if (screenPos.z >= 1.0f) continue;

            //float remain = beam.lockTime - musicTime;
            float total = beam.lockTime - beam.chargeTime;
            if (total < 0.01f) total = 0.01f;
            float t = std::clamp((musicTime - beam.chargeTime) / total, 0.0f, 1.0f);

            // 発光の脈動(sin波で明滅)、tが進むほど強くなる
            float pulse = 0.6f + 0.4f * sinf(musicTime * 12.0f);
            float radius = 6.0f + t * 14.0f;
            int alpha = (int)(180 + t * 75);

            ImGui::GetForegroundDrawList()->AddCircleFilled(
                ImVec2(screenPos.x, screenPos.y), radius* pulse,
                IM_COL32(255, 220, 150, alpha)
            );
            ImGui::GetForegroundDrawList()->AddCircle(
                ImVec2(screenPos.x, screenPos.y), radius* pulse * 1.6f,
                IM_COL32(255, 160, 60, alpha / 2), 24, 2.0f
            );

            //ImU32 col = IM_COL32((int)(255 * (1.0f - ratio)), (int)(255 * ratio), 60, 220);
            //ImGui::GetForegroundDrawList()->AddCircle(
            //    ImVec2(screenPos.x, screenPos.y), 25.0f * ratio + 8.0f, col, 32, 3.0f);
        }

        for (int i = 0; i < 6; i++)
        {
            if (m_judggeEffects[i].timer <= 0.0f) continue;

            auto pos3D = judggeCircles[i].GetPosition(now);
 /*           pos3D.y += 40.0f;
            pos3D.z += 40.0f;*/

            XMVECTOR worldVec = XMLoadFloat3(&pos3D);
            XMVECTOR screenVec = XMVector3Project(
                worldVec, 0.0f, 0.0f, screenW, screenH, 0.0f, 1.0f, m_proj, m_view, XMMatrixIdentity()
            );
            XMFLOAT3 screenPos;
            XMStoreFloat3(&screenPos, screenVec);
            if (screenPos.z >= 1.0f) continue;

            float age = 1.0f - (m_judggeEffects[i].timer / 0.5f);   //0->1
            DrawJuddgeBurst(ImGui::GetForegroundDrawList(),
                ImVec2(screenPos.x, screenPos.y), age, m_judggeEffects[i].rank);
            //ID3D11ShaderResourceView* tex =
            //    (m_judggeEffects[i].rank == 0) ? perfectTexture :
            //    (m_judggeEffects[i].rank == 1) ? greatTexture : goodTexture;
            //if (!tex) continue;

            //auto pos3D = judggeCircles[i].GetPosition(now);
            //pos3D.y += 40.0f;   //判定円より上
            //pos3D.z += 40.0f;

            //XMVECTOR worldVec = XMLoadFloat3(&pos3D);
            //XMVECTOR screenVec = XMVector3Project(
            //    worldVec, 0.0f, 0.0f, screenW, screenH, 0.0f, 1.0f, m_proj, m_view, XMMatrixIdentity()
            //);

            //XMFLOAT3 screenPos;
            //XMStoreFloat3(&screenPos, screenVec);
            //if (screenPos.z >= 1.0f) continue;

            ////RECT srcRect = { 0,0,128,64 };  //画像サイズに調整
            //RECT srcRect = { 0,0,778,247 };  //画像サイズに調整
            //DirectX::XMFLOAT2 origin = { 64.0f,32.0f };

            ////タイマー残量でフェードアウト
            //float alpha = m_judggeEffects[i].timer / 0.5f;
            //DirectX::XMVECTORF32 color = { 1.0f,1.0f,1.0f,alpha };

            //spriteBatch->Draw(tex,
            //    DirectX::XMFLOAT2(screenPos.x, screenPos.y),
            //    &srcRect, color, 0.0f, origin, 1.0f);
        }
        spriteBatch->End();

        // SpriteBatch後にステートを戻す
        context->OMSetBlendState(m_states->Opaque(), nullptr, 0xFFFFFFFF);
        context->OMSetDepthStencilState(m_states->DepthDefault(), 0);
        context->RSSetState(m_states->CullNone());
    }

    // ── 3Dモデル描画（note モデルがロードされている場合のみ）────
    //float now = (bgmstate == BGMstate::play)
    //    ? musicTime
    //    : editorBeat * (60.0f / bpm);

    if (m_noteModel)
    {
        for (int i = 0; i < (int)notes.size(); i++)
        {
            const auto& note = notes[i];
            if (note.note == FLICK_RIGHT || note.note == FLICK_LEFT || note.note == LONGNOTE) continue;
            float et = note.time;
            float st = note.GetSpawnTime();

            DirectX::SimpleMath::Vector3 pos;
            if (bgmstate != BGMstate::play)
            {
                pos = note.spawnPos;
            }
            else
            {
                if (now < st || now > et) continue; // 表示範囲外はスキップ
                pos = EvaluateNotePosition(note, now);
            }

            DirectX::SimpleMath::Vector3 tint;
            switch (note.lane)
            {
            case 0: tint = { 1.0f, 0.3f, 0.3f }; break; // 赤（S）
            case 1: tint = { 1.0f, 0.7f, 0.0f }; break; // 橙（D）
            case 2: tint = { 1.0f, 1.0f, 0.3f }; break; // 黄（F）
            case 3: tint = { 0.3f, 1.0f, 0.3f }; break; // 緑（J）
            case 4: tint = { 0.3f, 0.7f, 1.0f }; break; // 青（K）
            case 5: tint = { 0.7f, 0.3f, 1.0f }; break; // 紫（L）
            default: tint = { 1.0f, 1.0f, 1.0f }; break;
            }

            float scale = 10.0f;
            // スケール1.0で配置位置に移動するワールド行列
            DirectX::SimpleMath::Matrix world =
                DirectX::SimpleMath::Matrix::CreateScale(scale) *
                DirectX::SimpleMath::Matrix::CreateRotationY(3.2f) *
                DirectX::SimpleMath::Matrix::CreateTranslation(pos);

            m_noteModel->SetTintColor(tint);
            m_noteModel->Draw(context, *m_states, world, m_view, m_proj);

            // デバッグ用テキスト（毎フレームノーツ数分表示されるので重い。本番では削除推奨）
            //ImGui::Text("now: %.2f  displayStart: %.2f  et: %.2f", now, st, et);
        }
    }

    //横フリック描画
    if (m_flickModel)
    {
        for (int i = 0; i < (int)notes.size(); i++)
        {
            const auto& note = notes[i];
            if (note.note != FLICK_RIGHT && note.note != FLICK_LEFT) continue;
            float et = note.time;
            float st = note.GetSpawnTime();
            DirectX::SimpleMath::Vector3 pos;
            if (bgmstate != BGMstate::play)
            {
                pos = note.spawnPos;
            }
            else
            {
                if (now < st || now > et) continue; // 表示範囲外はスキップ
                pos = EvaluateNotePosition(note, now);
            }
			float dirScaleX = (note.note == FLICK_RIGHT) ? 1.0f : -1.0f;    //反転するか否か

            // スケールは常に正の値（反転は使わない）
            DirectX::SimpleMath::Vector3 tint =
                (note.note == FLICK_RIGHT)
                ? DirectX::SimpleMath::Vector3(1.0f, 0.4f, 0.4f)
                : DirectX::SimpleMath::Vector3(0.4f, 0.6f, 1.0f);

            float RotationX = (note.note == FLICK_RIGHT) ? 0.0f : 3.14f;
			float RotationZ = (note.note == FLICK_RIGHT) ? -1.57f : -1.57f;

            float scale = 2.0f;
            DirectX::SimpleMath::Matrix world =
                DirectX::SimpleMath::Matrix::CreateScale(scale) *
                DirectX::SimpleMath::Matrix::CreateRotationX(RotationX) *
                DirectX::SimpleMath::Matrix::CreateRotationZ(RotationZ) *
                DirectX::SimpleMath::Matrix::CreateTranslation(pos);

            m_flickModel->SetTintColor(tint);
            m_flickModel->Draw(context, *m_states, world, m_view, m_proj);
        }
    }


    {
        constexpr float kGraceTime = 0.5f; // arrivalTime を過ぎても動き続ける猶予時間
        constexpr float kBodySpacing = 0.005f;
		constexpr float kOverlapSentan = 0.025f; // dou/oに食い込ませる猶予（秒）。モデルサイズに応じて調整
		constexpr float kOverlapO = 0.005f; // sentan/douに食い込ませる猶予（秒）。モデルサイズに応じて調整


        for (int i = 0; i < (int)notes.size(); ++i)
        {
            auto& note = notes[i];
            if (note.note != LONGNOTE) continue;

            float displayStart = note.GetSpawnTime();
            float arrivalTime = note.arrivalTime;
            float endTime = note.time;
            float holdDuration = endTime - arrivalTime;

            if (arrivalTime - displayStart <= 0.0001f) continue;
            if (holdDuration <= 0.0001f) holdDuration = 0.0001f;

            float facingAngle = CalcFacingAngleY(note.spawnPos, note.judgePos);
            Matrix rotFlipped = Matrix::CreateRotationY(facingAngle + XM_PI);

            // 全体の表示可否（oが消えた後、余韻分だけ表示継続）
            float extendedEnd = endTime + kGraceTime;
            if (musicTime < displayStart || musicTime > extendedEnd) continue;

            // sentanはEndTimeまで進み続ける（endTime到達=oが判定ラインに届く瞬間）
            float sentanTime = std::min(musicTime, endTime);
            Vector3 sentanPos = EvaluateLongNotePos(note, sentanTime);

            {
                Matrix world = Matrix::CreateScale(20.0f) * rotFlipped * Matrix::CreateTranslation(sentanPos);
                if (m_Long_Sentan_Model) m_Long_Sentan_Model->Draw(context, *m_states, world, m_view, m_proj);
            }

            // oの現在時刻：sentanよりholdDurationだけ遅れて追従
            float tailTime = sentanTime - holdDuration;
            bool  tailVisible = (tailTime >= displayStart);
            if (tailVisible)
            {
                Vector3 oPos = EvaluateLongNotePos(note, tailTime);
                Matrix world = Matrix::CreateScale(20.0f) * rotFlipped * Matrix::CreateTranslation(oPos);
                if (m_Long_O_Model) m_Long_O_Model->Draw(context, *m_states, world, m_view, m_proj);
            }

            // dou（本体）：tailTime～sentanTimeを均等分割
            {
                float bodyStart = (tailVisible ? tailTime : displayStart) - kOverlapSentan;
                float bodyEnd = sentanTime + kOverlapO;
                bodyStart = std::max(bodyStart, displayStart);
                float bodyDuration = bodyEnd - bodyStart;
                if (bodyDuration > 0.0001f)
                {
                    int bodySegments = std::max(2, (int)std::ceil(bodyDuration / kBodySpacing));
                    for (int s = 0; s < bodySegments; ++s)
                    {
                        float p = (float)s / (float)(bodySegments - 1);
                        float sampleTime = bodyStart + bodyDuration * p;
                        Vector3 pos = EvaluateLongNotePos(note, sampleTime);
                        Matrix world = Matrix::CreateScale(10.0f) * rotFlipped * Matrix::CreateTranslation(pos);
                        if (m_Long_Dou_Model) m_Long_Dou_Model->Draw(context, *m_states, world, m_view, m_proj);
                    }
                }
            }
        }
    }

    //プレイヤー表示
    if (m_playerModel)
    {
        Vector3 pos = playerBasePos;
        pos.x += playerVisualX;

        Matrix world = Matrix::CreateScale(0.06f) * Matrix::CreateRotationX(1.5f) * Matrix::CreateTranslation(pos);
        m_playerModel->Draw(context, *m_states, world, m_view, m_proj);
    }

    //敵表示
    if (m_enemyModel)
    {
        Matrix world = Matrix::CreateScale(0.2f) * Matrix::CreateRotationX(1.5f) * Matrix::CreateRotationY(3.14f) * Matrix::CreateTranslation(enemyPos);
        m_enemyModel->Draw(context, *m_states, world, m_view, m_proj);
    }

    // ════════════════════════════════════════════
    // 下部バー UI（タイムライン・再生操作）
    // ════════════════════════════════════════════
    {
        ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomH));
        ImGui::SetNextWindowSize(ImVec2(screenW, bottomH));
        ImGui::Begin("BottomBar", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        // 再生 / 一時停止ボタン
        if (ImGui::Button("Play/Pause", ImVec2(100, 0)))
        {
            switch (bgmstate)
            {
            case BGMstate::stop:
                originalNotes = notes;
                musicTime = editorBeat * (60.0f / bpm);
                sound.SetPlayPositionSeconds(SOUND_LABEL_BGM001, musicTime);
                sound.Play(SOUND_LABEL_BGM001);
                bgmstate = BGMstate::play;
                break;
            case BGMstate::play:
                notes = originalNotes;
                success = 0; miss = 0;
                sound.Pause(SOUND_LABEL_BGM001);
                bgmstate = BGMstate::pause;
                break;
            case BGMstate::pause:
                sound.Resume(SOUND_LABEL_BGM001);
                bgmstate = BGMstate::play;
                break;
            }
        }
        ImGui::SameLine();
        // 先頭に戻るボタン
        if (ImGui::Button("BACK", ImVec2(80, 0)))
        {
            sound.Stop(SOUND_LABEL_BGM001);
            musicTime = 0.0f;
            bgmstate = BGMstate::stop;
            notes = originalNotes;
            success = 0; miss = 0;
        }

        // ── タイムラインスライダー ───────────────
        float secondsPerBeat = 60.0f / bpm;
        float timelineSeconds = (bgmstate == BGMstate::play)
            ? musicTime
            : editorBeat * secondsPerBeat;
        float songLengthSeconds = totalBeat * secondsPerBeat;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        bool timeChanged = ImGui::SliderFloat("Time", &timelineSeconds, 0.0f, songLengthSeconds, "%.2f sec");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        timeChanged |= ImGui::InputFloat("sec", &timelineSeconds, 0.1f, 1.0f, "%.2f");

        // スライダーまたは数値入力が変更されたら音楽位置を同期
        if (timeChanged)
        {
            if (timelineSeconds < 0.0f)              timelineSeconds = 0.0f;
            if (timelineSeconds > songLengthSeconds) timelineSeconds = songLengthSeconds;
            musicTime = timelineSeconds;
            editorBeat = timelineSeconds / secondsPerBeat;

            if (bgmstate == BGMstate::play)
                sound.SeekSeconds(SOUND_LABEL_BGM001, musicTime);
            else
                sound.SetPlayPositionSeconds(SOUND_LABEL_BGM001, musicTime);
        }

        // 時刻・スコア表示
        ImGui::SameLine();
        int  minutes = (int)(musicTime / 60);
        int  seconds = (int)(musicTime) % 60;
        char timeStr[64];
        sprintf_s(timeStr, "%02d:%02d  OK:%d MISS:%d", minutes, seconds, success, miss);
        ImGui::Text(timeStr);

        // 選択中ノーツの到達時刻・継続時間を下部バーからも編集可能
        if (selectedNoteIndex >= 0 && selectedNoteIndex < (int)notes.size())
        {
            Note& n = notes[selectedNoteIndex];
            float duration = n.time - n.GetSpawnTime();

            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::InputFloat("Arrive", &n.time, 0.1f, 1.0f, "%.2f"))
            {
                if (n.time <= n.GetSpawnTime()) n.time = n.GetSpawnTime() + 0.01f;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::InputFloat("Dur", &duration, 0.1f, 1.0f, "%.2f"))
            {
                if (duration < 0.01f) duration = 0.01f;
                n.time = n.GetSpawnTime() + duration; // Duration から Arrive を逆算
            }
        }

        ImGui::End();
    }

    m_deviceResources->PIXEndEvent();

    // ImGui の描画データを DX11 に渡して実際に描く
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    m_deviceResources->Present(); // バックバッファを画面に表示
}

// ─────────────────────────────────────────────
// Clear : バックバッファとデプス・ステンシルをクリアする
// ─────────────────────────────────────────────
void Game::Clear()
{
    m_deviceResources->PIXBeginEvent(L"Clear");

    auto context = m_deviceResources->GetD3DDeviceContext();
    auto renderTarget = m_deviceResources->GetRenderTargetView();
    auto depthStencil = m_deviceResources->GetDepthStencilView();

    context->ClearRenderTargetView(renderTarget, Colors::CornflowerBlue); // 背景色（コーンフラワーブルー）
    context->ClearDepthStencilView(depthStencil,
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    context->OMSetRenderTargets(1, &renderTarget, depthStencil);

    auto const viewport = m_deviceResources->GetScreenViewport();
    context->RSSetViewports(1, &viewport);

    m_deviceResources->PIXEndEvent();
}
#pragma endregion

#pragma region Message Handlers
void Game::OnActivated() {}
void Game::OnDeactivated() {}
void Game::OnSuspending() {}

void Game::OnResuming()
{
    m_timer.ResetElapsedTime();
}

void Game::OnWindowMoved()
{
    auto const r = m_deviceResources->GetOutputSize();
    m_deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnDisplayChange()
{
    m_deviceResources->UpdateColorSpace();
}

void Game::OnWindowSizeChanged(int width, int height)
{
    if (!m_deviceResources->WindowSizeChanged(width, height)) return;
    CreateWindowSizeDependentResources();
}
#pragma endregion

// ─────────────────────────────────────────────
// SaveChart : 譜面を JSON ファイルへ保存
//
//   出力先: charts/<name>.json
//   フォーマット:
//     {
//       "meta": { "bpm": 120, "totalBeat": 64 },
//       "notes": [ { lane, type, time, spawnTime, spawnPos, judgePos, path? }, ... ],
//       "beams": [ { lane, startTime, endTime, warningTime }, ... ],
//       "judgeCircles": [ { "lane": 0, "keyframe": [ {time,x,y,z}, ... ] }, ... ]
//     }
// ─────────────────────────────────────────────
//namespace fs = std::filesystem; // IntelliSenseの誤検知対策も兼ねたエイリアス
// ─────────────────────────────────────────────
// SaveChart : 譜面を JSON ファイルへ保存
// ─────────────────────────────────────────────
void Game::SaveChart(std::string name)
{
    std::error_code ec;
    std::filesystem::create_directories("charts", ec);
    if (ec)
    {
        g_chartStatusMessage = "Save failed: cannot create 'charts' folder";
        g_chartStatusIsError = true;
        OutputDebugStringA(("[SaveChart]" + g_chartStatusMessage + "\n").c_str());
        return;
    }
    std::ofstream file("charts/" + name + ".json");
    if (!file.is_open())
    {
        g_chartStatusMessage = "Save failed: cannot open file";
        g_chartStatusIsError = true;
        OutputDebugStringA(("[SaveChart]" + g_chartStatusMessage + "\n").c_str());
        return;
    }

    file << "{\n";
    // メタ情報
    file << "\"meta\":{"
        << "\"bpm\":" << bpm << ","
        << "\"totalBeat\":" << totalBeat
        << "},\n";

    // ノーツ
    file << "\"notes\":[\n";
    for (size_t i = 0; i < notes.size(); i++)
    {
        const auto& n = notes[i];
        file << "{";
        file << "\"lane\":" << n.lane << ",";
        file << "\"type\":" << n.note << ",";
        file << "\"time\":" << n.time;               // End time

        if (n.spawnTime >= 0.0f)
            file << ",\"spawnTime\":" << n.spawnTime;

        // arrivalTime（ロングノーツの先端到達時刻。TAP等でも念のため保存）
        file << ",\"arrivalTime\":" << n.arrivalTime;

        // フリック終点レーン（未設定なら-1のまま保存）
        file << ",\"flickLaneTo\":" << n.flickLaneTo;

        // ロングノーツの保持時間
        file << ",\"holdDuration\":" << n.holdDuration;

        file << ",\"spawnPos\":{"
            << "\"x\":" << n.spawnPos.x << ","
            << "\"y\":" << n.spawnPos.y << ","
            << "\"z\":" << n.spawnPos.z << "}";

        file << ",\"judgePos\":{"
            << "\"x\":" << n.judgePos.x << ","
            << "\"y\":" << n.judgePos.y << ","
            << "\"z\":" << n.judgePos.z << "}";

        if (!n.controlPoints.empty())
        {
            file << ",\"path\":[";
            for (size_t p = 0; p < n.controlPoints.size(); p++)
            {
                auto& cp = n.controlPoints[p];
                file << "{\"x\":" << cp.x
                    << ",\"y\":" << cp.y
                    << ",\"z\":" << cp.z << "}";
                if (p != n.controlPoints.size() - 1) file << ",";
            }
            file << "]";
        }
        file << "}";
        if (i != notes.size() - 1) file << ",";
        file << "\n";
    }
    file << "],\n";

    // ビーム（配置エディタ用のBeamsリスト）
    file << "\"beams\":[\n";
    for (size_t i = 0; i < beams.size(); i++)
    {
        file << "{";
        file << "\"lane\":" << beams[i].lane << ",";
        file << "\"startTime\":" << beams[i].startTime << ",";
        file << "\"endTime\":" << beams[i].endTime << ","
            << "\"warningTime\":" << beams[i].warningTime;
        file << "}";
        if (i != beams.size() - 1) file << ",";
        file << "\n";
    }
    file << "],\n";

    // ホーミングビーム（homingBeams）
    file << "\"homingBeams\":[\n";
    for (size_t i = 0; i < homingBeams.size(); i++)
    {
        const auto& b = homingBeams[i];
        file << "{";
        file << "\"chargeTime\":" << b.chargeTime << ",";
        file << "\"lockTime\":" << b.lockTime << ",";
        file << "\"hitTime\":" << b.hitTime;
        file << "}";
        if (i != homingBeams.size() - 1) file << ",";
        file << "\n";
    }
    file << "],\n";

    // 判定円のキーフレーム
    file << "\"judgeCircles\":[\n";
    for (size_t i = 0; i < judggeCircles.size(); i++)
    {
        auto& circle = judggeCircles[i];
        file << "{\"lane\":" << circle.lane << ",\"keyframe\":[";
        for (size_t k = 0; k < circle.keyframe.size(); k++)
        {
            auto& key = circle.keyframe[k];
            file << "{\"time\":" << key.time
                << ",\"x\":" << key.x
                << ",\"y\":" << key.y
                << ",\"z\":" << key.z << "}";
            if (k != circle.keyframe.size() - 1) file << ",";
        }
        file << "]}";
        if (i != judggeCircles.size() - 1) file << ",";
        file << "\n";
    }
    file << "]\n}\n";

    file.close();

    if (file.fail())
    {
        g_chartStatusMessage = "Save failed: write error";
        g_chartStatusIsError = true;
    }
    else
    {
        g_chartStatusMessage = "Saved charts/" + name + ".json (notes:" +
            std::to_string(notes.size()) + ", beams:" + std::to_string(homingBeams.size()) + ")";
        g_chartStatusIsError = false;
    }
    OutputDebugStringA(("[SaveChart]" + g_chartStatusMessage + "\n").c_str());
}

// ─────────────────────────────────────────────
// LoadChart : JSON ファイルから譜面を読み込む
// ─────────────────────────────────────────────
void Game::LoadChart(std::string name)
{
    std::ifstream file("charts/" + name + ".json");
    if (!file.is_open())
    {
        g_chartStatusMessage = "Load failed: file not found(charts/" + name + ".json)";
        g_chartStatusIsError = true;
        OutputDebugStringA(("[LoadChart]" + g_chartStatusMessage + "\n").c_str());
        return;
    }
    nlohmann::json j;
    try
    {
        file >> j;
    }
    catch (const std::exception& e)
    {
        g_chartStatusMessage = std::string("Load failed:invalid JSON(") + e.what() + ")";
        g_chartStatusIsError = true;
        OutputDebugStringA(("[LoadChart]" + g_chartStatusMessage + "\n").c_str());
        return;
    }
    notes.clear();
    beams.clear();
    homingBeams.clear();

    // メタ情報（なければ現状維持）
    if (j.contains("meta"))
    {
        if (j["meta"].contains("bpm")) bpm = j["meta"]["bpm"].get<float>();
        if (j["meta"].contains("totalBeat")) totalBeat = j["meta"]["totalBeat"].get<float>();
    }

    // ノーツ
    if (j.contains("notes"))
    {
        for (const auto& n : j["notes"])
        {
            Note note;
            note.lane = n["lane"].get<int>();
            note.note = static_cast<NoteType>(n["type"].get<int>());
            note.time = n["time"].get<float>();

            if (n.contains("spawnTime"))
                note.spawnTime = n["spawnTime"].get<float>();

            if (n.contains("spawnPos"))
            {
                note.spawnPos.x = n["spawnPos"]["x"].get<float>();
                note.spawnPos.y = n["spawnPos"]["y"].get<float>();
                note.spawnPos.z = n["spawnPos"]["z"].get<float>();
            }
            else
            {
                float laneX = -310.0f + (660.0f / 6.0f) * note.lane + (660.0f / 12.0f);
                note.spawnPos = { laneX, -55.0f, 1000.0f };
            }

            if (n.contains("judgePos"))
            {
                note.judgePos.x = n["judgePos"]["x"].get<float>();
                note.judgePos.y = n["judgePos"]["y"].get<float>();
                note.judgePos.z = n["judgePos"]["z"].get<float>();
            }
            else
            {
                float laneX = -310.0f + (660.0f / 6.0f) * note.lane + (660.0f / 12.0f);
                note.judgePos = { laneX, -55.0f, -210.0f };
            }

            // arrivalTime（旧フォーマット互換：無ければ time と同じにしておく）
            if (n.contains("arrivalTime"))
                note.arrivalTime = n["arrivalTime"].get<float>();
            else
                note.arrivalTime = note.time;

            // フリック終点レーン（無ければ未設定=-1）
            if (n.contains("flickLaneTo"))
                note.flickLaneTo = n["flickLaneTo"].get<int>();
            else
                note.flickLaneTo = -1;

            // ロングノーツの保持時間（無ければ arrivalTime と time の差から逆算）
            if (n.contains("holdDuration"))
                note.holdDuration = n["holdDuration"].get<float>();
            else
                note.holdDuration = std::max(0.05f, note.time - note.arrivalTime);

            if (n.contains("path"))
            {
                for (const auto& p : n["path"])
                {
                    DirectX::SimpleMath::Vector3 v;
                    v.x = p["x"].get<float>();
                    v.y = p["y"].get<float>();
                    v.z = p["z"].get<float>();
                    note.controlPoints.emplace_back(v);
                }
            }
            notes.emplace_back(note);
        }
        std::sort(notes.begin(), notes.end(),
            [](const Note& a, const Note& b) { return a.time < b.time; });
    }

    // ビーム
    if (j.contains("beams"))
    {
        for (const auto& b : j["beams"])
        {
            beams.push_back(Beams(
                b["lane"].get<int>(),
                b["startTime"].get<float>(),
                b["endTime"].get<float>(),
                b["warningTime"].get<float>()
            ));
        }
    }

    // ホーミングビーム
    if (j.contains("homingBeams"))
    {
        for (const auto& b : j["homingBeams"])
        {
            HomingBeam hb;
            hb.chargeTime = b["chargeTime"].get<float>();
            hb.lockTime = b["lockTime"].get<float>();
            hb.hitTime = b["hitTime"].get<float>();
            InitBeamScatter(hb);
            homingBeams.push_back(hb);
        }
        std::sort(homingBeams.begin(), homingBeams.end(),
            [](const HomingBeam& a, const HomingBeam& c) { return a.lockTime < c.lockTime; });
    }

    // 判定円のキーフレーム
    if (j.contains("judgeCircles"))
    {
        for (const auto& jc : j["judgeCircles"])
        {
            int lane = jc["lane"].get<int>();
            if (lane < 0 || lane >= (int)judggeCircles.size()) continue;

            auto& circle = judggeCircles[lane];
            circle.keyframe.clear();
            for (const auto& k : jc["keyframe"])
            {
                JudggeCircleframe key;
                key.time = k["time"].get<float>();
                key.x = k["x"].get<float>();
                key.y = k["y"].get<float>();
                key.z = k["z"].get<float>();
                circle.keyframe.push_back(key);
            }
            std::sort(circle.keyframe.begin(), circle.keyframe.end(),
                [](const JudggeCircleframe& a, const JudggeCircleframe& b) {
                    return a.time < b.time;
                });
        }
    }

    // ロード後の状態リセット
    sound.Stop(SOUND_LABEL_BGM001);
    bgmstate = BGMstate::stop;
    musicTime = 0.0f;
    editorBeat = 0.0f;
    success = 0;
    miss = 0;
    perfect = 0;
    great = 0;
    good = 0;
    selectedNoteIndex = -1;
    m_EditorState = EditorState::IDLE;
    originalNotes = notes;

    g_chartStatusMessage = "Loaded: charts/" + name + ".json (notes:" +
        std::to_string(notes.size()) + ", beams:" + std::to_string(homingBeams.size()) + ")";
    g_chartStatusIsError = false;
    OutputDebugStringA(("[LoadChart]" + g_chartStatusMessage + "\n").c_str());
}


void Game::JudggeLane(char key, int laneIndex)
{
    //if (!(GetAsyncKeyState(key) & 0x0001)) return;  //押した瞬間
    bool isDown = (GetAsyncKeyState(key) & 0x8000) != 0;    // 押しっぱなし状態(held)を見る
    bool pressedThisFrame = isDown && !m_prevLaneKeyDown[laneIndex]; // 前フレームは離れていて、今フレームは押されている=押した瞬間
    m_prevLaneKeyDown[laneIndex] = isDown; // 次フレーム用に記録

    if (!pressedThisFrame) return; // 押した瞬間でなければ何もしない

    //TAPノーツの判定
    int targetLane = laneIndex;
    int bestIndex = -1;
    float bestDiff = 9999.9f;   //大きい数字を一回代入

    //全ノーツを走査して近いものを探す
    for (int i = 0; i < (int)notes.size(); i++)
    {
        if(notes[i].lane != targetLane) continue;
        if(notes[i].note == FLICK_RIGHT || notes[i].note == FLICK_LEFT) continue;
		if (notes[i].note == LONGNOTE) continue;   //ロングノーツは判定しない

        float diff = fabsf(notes[i].time - musicTime);  //時間差で判定
        if (diff < bestDiff)
        {
            bestDiff = diff;
            bestIndex = i;
        }
    }
    //if (bestIndex == -1) return;

    //if (bestDiff > kGud) return;    //判定圏外ならスルー

    if(bestIndex != -1 && bestDiff <= kGud)
    {
        int rank =
            (bestDiff < kPerfect) ? 0 :
            (bestDiff < kGreat) ? 1 : 2;
        m_judggeEffects[laneIndex].rank = rank;
        m_judggeEffects[laneIndex].timer = 0.5f;
        if (rank == 0)
        {
            perfect++;
        }
        else if (rank == 1)
        {
            great++;
        }
        else if (rank == 2)
        {
            good++;
        }
        //sound.Play(タップの音);

        notes.erase(notes.begin() + bestIndex);
        success++;
        return; //TAPを処理したからロングの処理はしない
    }

 //   //ロングノーツの判定
 //   int bestLongIndex = -1;
	//float bestLongDiff = 9999.9f;

 //   for(int i = 0; i < (int)notes.size(); i++)
 //   {
 //       if(notes[i].lane != targetLane) continue;
 //       if(notes[i].note != LONGNOTE) continue;
	//	if (notes[i].longStarted || notes[i].longFailed) continue;   //すでに判定済みのロングノーツはスルー

 //       float diff = fabsf(notes[i].arrivalTime - musicTime);  //timeではなくarrivalTimeで判定
 //       if (diff < bestLongDiff)
 //       {
 //           bestLongDiff = diff;
 //           bestLongIndex = i;
 //       }
	//}
 //   if (bestLongIndex != -1 && bestLongDiff <= kGud)
 //   {
	//	auto& n = notes[bestLongIndex];
 //       int rank =
 //           (bestLongDiff < kPerfect) ? 0 :
	//		(bestLongDiff < kGreat) ? 1 : 2;

 //       m_judggeEffects[laneIndex].rank = rank;
 //       m_judggeEffects[laneIndex].timer = 0.5f;
 //       if (rank == 0)
 //       {
 //           perfect++;
 //       }
 //       else if (rank == 1)
 //       {
 //           great++;
 //       }
 //       else if (rank == 2)
 //       {
 //           good++;
	//	}
	//	//sound.Play(ロングの音);
 //       n.longStarted = true;
 //       n.nextTickTime = n.arrivalTime + kLongTickInterval;
 //   }
}

// JudgeFlickNotes : 横フリック判定（時間ベース）
//
//   毎フレーム呼び出し、判定ウィンドウ(±kGud)に入っている間、
//   プレイヤーが要求サイドにいれば時間差でランク判定して成立。
//   ウィンドウを過ぎてしまったらミス。
void Game::JudggeFlickNotes()
{
    for (int i = (int)notes.size() - 1; i >= 0; i--)
    {
        auto& n = notes[i];
		if (n.note != NoteType::FLICK_RIGHT && n.note != NoteType::FLICK_LEFT) continue;

        float diff = n.time - musicTime;    //正：来てない　負：過ぎた
		if (diff > kGud) continue;   //判定圏外
		int requiredSide = (n.note == NoteType::FLICK_RIGHT) ? 1 : -1;  // 右フリックなら1、左フリックなら-1

        if(fabsf(diff) <= kGud && playerSide == requiredSide)
        {
            char dbgBuf[128];
            sprintf_s(dbgBuf, "[FlickJudge] n.lane=%d n.flickLaneTo=%d\n", n.lane, n.flickLaneTo);
            OutputDebugStringA(dbgBuf);

			float absDiff = fabsf(diff);    //絶対値で判定
            int rank =
                (absDiff < kPerfect) ? 0 :
                (absDiff < kGreat) ? 1 : 2;
    //        if (n.lane >= 0 && n.lane < 6)
    //        {
				//m_judggeEffects[n.lane].rank = rank;    // 判定エフェクトのランクを設定
				//m_judggeEffects[n.lane].timer = 0.5f;   // 判定エフェクトの表示時間をリセット
    //        }

			//レーン範囲（From~To）を求めて、その範囲内のレーンに判定エフェクトを表示する
            int laneFrom = n.lane;
			int laneTo = (n.flickLaneTo >= 0) ? n.flickLaneTo : n.lane; // flickLaneTo が -1 の場合は lane と同じにする
			int lo = std::min(laneFrom, laneTo);    // レーン範囲の下限
			int hi = std::max(laneFrom, laneTo);    // レーン範囲の上限
            for (int L = lo; L <= hi; L++)
            {
				if (L < 0 || L >= 6) continue; // 範囲外のレーンはスキップ
                m_judggeEffects[L].rank = rank;    // 判定エフェクトのランクを設定
                m_judggeEffects[L].timer = 0.5f;   // 判定エフェクトの表示時間をリセット
            }

            if (rank == 0)
            {
                perfect++;
            }
            else if (rank == 1)
            {
                great++;
            }
            else if (rank == 2)
            {
                good++;
            }
            notes.erase(notes.begin() + i);
            success++;
            continue;
        }

        if(diff < -kGud)
        {
            //判定圏外で成立せず、過ぎてしまった場合はミス
            notes.erase(notes.begin() + i);
            miss++;
		}
    }
}

// JudggeLongNoteHold : 開始済みのロングノーツを毎フレームチェックする
// 押しっぱなしでない場合 → 即ミス
// endTime まで押しっぱなしだった場合 → パフェ(succcess++だけ加算、追加ランクなし)
void Game::JudggeLongNoteHold()
{
    for (int i = (int)notes.size() - 1; i >= 0; i--)
    {
        auto& n = notes[i];
        if (n.note != NoteType::LONGNOTE) continue;
        if (n.lane < 0 || n.lane >= (int)std::size(kLaneKeys)) continue;

        char key = kLaneKeys[n.lane].key;
        bool isDown = (GetAsyncKeyState(key) & 0x8000) != 0;

        // ── まだ開始していない場合：開始判定 ──
        if (!n.longStarted && !n.longFailed)
        {
            float diff = n.arrivalTime - musicTime;
            if (isDown && fabsf(diff) <= kGud)
            {
                float absDiff = fabsf(diff);
                int rank = (absDiff < kPerfect) ? 0 : (absDiff < kGreat) ? 1 : 2;
                m_judggeEffects[n.lane].rank = rank;
                m_judggeEffects[n.lane].timer = 0.5f;
                if (rank == 0) perfect++;
                else if (rank == 1) great++;
                else good++;

                n.longStarted = true;
                n.nextTickTime = n.arrivalTime + kLongTickInterval;
            }
            else if (diff < -kGud)
            {
                // 開始を逃した → Failed扱いにするだけで、ノーツは消さない
                n.longFailed = true;
                miss++;
            }
            // ウィンドウ内で待機中はまだ何もしない（生存させ続ける）
        }
        // ── 開始済み・まだ成功継続中 ──
        else if (n.longStarted && !n.longFailed)
        {
            if (isDown)
            {
                while (n.nextTickTime <= musicTime && n.nextTickTime < n.time)
                {
                    perfect++;
                    m_judggeEffects[n.lane].rank = 0;
                    m_judggeEffects[n.lane].timer = 0.2f;
                    // sound.Play(継続音);
                    n.nextTickTime += kLongTickInterval;
                }
            }
            else
            {
                // 離した瞬間 → Failedにするが、ノーツはEndTimeまで残す（見た目は最後まで流れる）
                n.longFailed = true;
                miss++;
            }
        }

        // ── 終端に到達したら消去（ここでのみ notes.erase する）──
        if (musicTime >= n.time)
        {
            if (!n.longFailed) success++;
            notes.erase(notes.begin() + i);
        }
    }
}

void Game::UpdateCamera()
{
    //状態更新----------------------------------------------------
    int prev = m_camState;
    bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool iBtn = (GetAsyncKeyState('I') & 0x8000) != 0;
    bool oBtn = (GetAsyncKeyState('O') & 0x8000) != 0;
    bool pBtn = (GetAsyncKeyState('P') & 0x8000) != 0;

    if (altDown)
    {
        m_camState = 0;
        if (iBtn) m_camState = 1;   //ORBIT
        if (oBtn) m_camState = 2;   //TRACK
        if (pBtn) m_camState = 3;   //DOLLY
    }
    else if (pBtn)
    {
        m_camState = 4; //FLIGHT
    }
    else
    {
        m_camState = 0;
    }

    if (prev != m_camState)
    {
        GetCursorPos(&m_camOldPos);
    }
    if (m_camState == 0) return;

    //マウス移動量
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    float mouseDX = (float)(cursorPos.x - m_camOldPos.x);
    float mouseDY = (float)(cursorPos.y - m_camOldPos.y);
    m_camOldPos = cursorPos;

    //カメラ姿勢ベクトル計算
    XMVECTOR vPos = XMLoadFloat3(&m_camPos);
    XMVECTOR vLook = XMLoadFloat3(&m_camLook);
    XMVECTOR vUp = XMVector3Normalize(XMLoadFloat3(&m_camUp));
    XMVECTOR vFront = XMVector3Normalize(XMVectorSubtract(vLook, vPos));
    XMVECTOR vSide = XMVector3Normalize(XMVector3Cross(vUp, vFront));
    vUp = XMVector3Normalize(XMVector3Cross(vFront, vSide));

    if (XMVector3Equal(vFront, XMVectorZero())) return;
    vFront = XMVector3Normalize(vFront);

    if (XMVector3Equal(vSide, XMVectorZero())) return;
    vSide = XMVector3Normalize(vSide);

    vUp = XMVector3Normalize(XMVector3Cross(vFront, vSide));


    XMVECTOR vFrontLen = XMVectorSubtract(vLook, vPos);
    float focus;
    XMStoreFloat(&focus, XMVector3Length(vFrontLen));
    if (focus < 0.001f) return;

    switch (m_camState)
    {
    case 1: //ORBIT
    {
        float angleX = 360.0f * mouseDX / 1280.0f;
        float angleY = 180.0f * mouseDY / 720.0f;

        XMMATRIX matRotY = XMMatrixRotationY(XMConvertToRadians(angleX));
        XMVECTOR vAxis = XMVector3Normalize(XMVector3TransformCoord(vSide, matRotY));
        XMMATRIX matRotSide = XMMatrixRotationAxis(vAxis, XMConvertToRadians(angleY));
        XMVECTOR vRelative = XMVector3TransformCoord(
            XMVectorScale(vFront, focus), matRotY * matRotSide
        );
        XMStoreFloat3(&m_camPos, XMVectorSubtract(vLook, vRelative));
        XMStoreFloat3(&m_camUp, XMVector3Normalize(
            XMVector3Cross(XMVector3Normalize(vRelative), vAxis)));
        break;
    }
    case 2: //TRACK
    {
        float farScreenHeight = tanf(m_camFovy * 0.5f) * m_camFar;
        float farMoveX = -farScreenHeight * (mouseDX / 640.0f) * m_camAspect;
        float farMoveY = farScreenHeight * (mouseDY / 360.0f);
        float rate = focus / m_camFar;

        XMVECTOR vMove = XMVectorZero();
        vMove = XMVectorAdd(vMove, XMVectorScale(vSide, farMoveX * rate));
        vMove = XMVectorAdd(vMove, XMVectorScale(vUp, farMoveY * rate));
        XMStoreFloat3(&m_camPos, XMVectorAdd(vPos, vMove));
        XMStoreFloat3(&m_camLook, XMVectorAdd(vLook, vMove));
        break;
    }
    case 3: //DOLLY
    {
        float rate = focus / m_camFar;
        XMVECTOR vMove = XMVectorScale(vFront,
            m_camFar * 0.01f * rate * (mouseDX + mouseDY));
        XMStoreFloat3(&m_camPos, XMVectorAdd(vPos, vMove));
        break;
    }
    case 4: //FLIGHT
    {
        float angleX = 360.0f * mouseDX / 1280.0f;
        float angleY = 180.0f * mouseDY / 720.0f;

        XMMATRIX matUpRot = XMMatrixRotationY(XMConvertToRadians(angleX));
        XMVECTOR vSideAxis = XMVector3Normalize(XMVector3TransformCoord(vSide, matUpRot));
        XMMATRIX matSideRot = XMMatrixRotationAxis(vSideAxis, XMConvertToRadians(angleY));
        XMVECTOR vFrontAxis = XMVector3Normalize(
            XMVector3TransformCoord(vFront, matUpRot * matSideRot));

        XMVECTOR vMove = XMVectorZero();
        if (GetAsyncKeyState('W') & 0x8000) vMove = XMVectorAdd(vMove, vFrontAxis);
        if (GetAsyncKeyState('S') & 0x8000) vMove = XMVectorSubtract(vMove, vFrontAxis);
        if (GetAsyncKeyState('A') & 0x8000) vMove = XMVectorSubtract(vMove, vSideAxis);
        if (GetAsyncKeyState('D') & 0x8000) vMove = XMVectorAdd(vMove, vSideAxis);
        vMove = XMVectorScale(vMove, 5.0f);

        XMVECTOR vNewPos = XMVectorAdd(vPos, vMove);

        constexpr float k0rbitSpeed = 0.1f;
        float pitchDeg = 0.0f;
        if (GetAsyncKeyState('Q') & 0x8000) pitchDeg += k0rbitSpeed;
        if (GetAsyncKeyState('E') & 0x8000) pitchDeg -= k0rbitSpeed;
        float yawDeg = 0.0f;
        if (GetAsyncKeyState('C') & 0x8000) yawDeg -= k0rbitSpeed;
        if (GetAsyncKeyState('Z') & 0x8000) yawDeg += k0rbitSpeed;
        XMVECTOR vRadius = XMVectorSubtract(vNewPos, vLook);
        if (pitchDeg != 0.0f)
        {
            XMMATRIX matPitch = XMMatrixRotationAxis(vSideAxis, XMConvertToRadians(pitchDeg));
            vRadius = XMVector3TransformCoord(vRadius, matPitch);
        }
        if (yawDeg != 0.0f)
        {
            XMVECTOR vUpAxis = XMVector3Normalize(vUp);
            XMMATRIX matYaw = XMMatrixRotationAxis(vUpAxis, XMConvertToRadians(yawDeg));
            vRadius = XMVector3TransformCoord(vRadius, matYaw);
        }
        vNewPos = XMVectorAdd(vLook, vRadius);

        XMStoreFloat3(&m_camPos, vNewPos);
        XMStoreFloat3(&m_camLook, XMVectorAdd(vNewPos,
            XMVectorScale(vFrontAxis, focus)));
        XMStoreFloat3(&m_camUp, XMVector3Normalize(
            XMVector3Cross(vFrontAxis, vSideAxis)));
        break;
    }

    }
}

// InitBeamScatter : ビームの「収束前バラけ位置」をランダムに決める
// プレイヤーの周囲、上下左右にランダムなオフセットを複数用意する
void Game::InitBeamScatter(HomingBeam& beam)
{
    for (int i = 0; i < HomingBeam::kScatterCount; i++)
    {
        float angle = ((float)rand() / RAND_MAX) * XM_2PI;
        float radius = 150.0f + ((float)rand() / RAND_MAX) * 250.0f;    //150~400の範囲でばらける
        float heightOffset = ((float)rand() / RAND_MAX - 0.5f) * 200.0f;    //高さ

        beam.scatterOffsets[i] = Vector3(
            cosf(angle) * radius,
            heightOffset,
            sinf(angle) * radius
        );
    }
    beam.scatterInitialized = true;
}

// ─────────────────────────────────────────────
// NoteExit : 指定レーン・時刻にノーツが存在するか確認
//   lane : 対象レーン
//   time : 判定時刻
//   戻り値: 存在すれば true（誤差 0.01 秒以内）
// ─────────────────────────────────────────────
bool Game::NoteExit(int lane, float time)
{
    for (const auto& n : notes)
    {
        if (n.lane == lane && fabs(n.time - time) < 0.01f)
            return true;
    }
    return false;
}

// ─────────────────────────────────────────────
// GetMirrorLane : ミラーモード時のレーン番号変換
//   ※ 現在は実装が未完成
// ─────────────────────────────────────────────
int Game::GetMirrorLane(int lane)
{
    if (!mirrorMode) return lane;
    // TODO: mirrorLane を基点とした左右反転を実装する
    return lane; // 仮の戻り値（要修正）
}

// デフォルトウィンドウサイズ（最小 320x200 以上）
void Game::GetDefaultSize(int& width, int& height) const noexcept
{
    width = 1920;
    height = 1080;
}

// DrawThickBeam : from→to を結ぶ、太さ指定可能なビームをクワッドで描画
// カメラ方向を基準に垂直な面を作ることで、常にカメラ側を向く
//void Game::DrawThickBeam(const DirectX::SimpleMath::Vector3& from, const DirectX::SimpleMath::Vector3& to, float thickness, DirectX::XMVECTORF32 color)
//{
//	Vector3 dir = to - from;
//    float len = dir.Length();
//	if (len < 0.01f) return; // 長さがほぼゼロなら描画しない
//	dir.Normalize();
//
//	Vector3 toCam = m_camPos - from;
//	Vector3 side = dir.Cross(toCam);    // ビーム方向とカメラ方向の外積で垂直ベクトルを求める
//	if (side.LengthSquared() < 0.0001f) side = Vector3::Up; // 外積がほぼゼロなら上方向を代用
//	side.Normalize();
//	side *= thickness * 0.5f;   // ビームの半分の厚さ
//
//	Vector3 v0 = from - side; // クワッドの4頂点
//	Vector3 v1 = from + side;
//	Vector3 v2 = to + side;
//	Vector3 v3 = to - side;
//
//	m_primitiveBatch->DrawQuad( // 4頂点のクワッドを描画
//        VertexPositionColor(v0, color),
//        VertexPositionColor(v1, color),
//        VertexPositionColor(v2, color),
//        VertexPositionColor(v3, color)
//	);
//}

void Game::DrawThickBeam(const DirectX::SimpleMath::Vector3& from, const DirectX::SimpleMath::Vector3& to, float thicknessFrom, float thicknessTo, DirectX::XMVECTORF32 coreColor, DirectX::XMVECTORF32 glowColor)
{
	Vector3 dir = to - from;
    float len = dir.Length();
	if (len < 0.01f) return; // 長さがほぼゼロなら描画しない
    dir.Normalize();

	Vector3 toCam = m_camPos - from;
	Vector3 side = dir.Cross(toCam);    // ビーム方向とカメラ方向の外積で垂直ベクトルを求める
	if (side.LengthSquared() < 0.0001f) side = Vector3::Up; // 外積がほぼゼロなら上方向を代用
	side.Normalize();

	//外側のグロー部分
    {
        Vector3 sideFrom = side * (thicknessFrom * 1.8f * 0.5f);
        Vector3 sideTo = side * (thicknessTo * 1.8f * 0.5f);
        m_primitiveBatch->DrawQuad(
            VertexPositionColor(from - sideFrom, glowColor),
            VertexPositionColor(from + sideFrom, glowColor),
            VertexPositionColor(to + sideTo, glowColor),
            VertexPositionColor(to - sideTo, glowColor)
        );
    }
    //  芯(コア)層(細め・明るい白寄り)
    {
        Vector3 sideFrom = side * (thicknessFrom * 0.5f);
        Vector3 sideTo = side * (thicknessTo * 0.5f);
        m_primitiveBatch->DrawQuad(
            VertexPositionColor(from - sideFrom, coreColor),
            VertexPositionColor(from + sideFrom, coreColor),
            VertexPositionColor(to + sideTo, coreColor),
            VertexPositionColor(to - sideTo, coreColor)
        );
    }
}


#pragma region Direct3D Resources

// ─────────────────────────────────────────────
// CreateDeviceDependentResources : D3D デバイス依存リソースの生成
//   ・CommonStates（BlendState等のプリセット）
//   ・PrimitiveBatch（ライン・クワッドのインスタント描画）
//   ・BasicEffect（頂点カラーシェーダ）
//   ・InputLayout（頂点フォーマット）
// ─────────────────────────────────────────────
void Game::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();

    m_states = std::make_unique<DirectX::CommonStates>(device);

    m_primitiveBatch =
        std::make_unique<DirectX::PrimitiveBatch<DirectX::VertexPositionColor>>(
            m_deviceResources->GetD3DDeviceContext());

    m_basicEffect = std::make_unique<DirectX::BasicEffect>(device);
    m_basicEffect->SetVertexColorEnabled(true); // 頂点ごとの色を有効化

    // BasicEffect の頂点シェーダバイトコードから InputLayout を生成
    void const* shaderByteCode;
    size_t      byteCodeLength;
    m_basicEffect->GetVertexShaderBytecode(&shaderByteCode, &byteCodeLength);

    DX::ThrowIfFailed(
        device->CreateInputLayout(
            DirectX::VertexPositionColor::InputElements,
            DirectX::VertexPositionColor::InputElementCount,
            shaderByteCode,
            byteCodeLength,
            m_inputLayout.ReleaseAndGetAddressOf()
        )
    );

    // テクスチャ・モデルのロード（現在コメントアウト中）
}

void Game::CreateWindowSizeDependentResources() {}

// ─────────────────────────────────────────────
// SnapToGrid : 時刻を BPM グリッドにスナップする
//   time     : スナップしたい時刻（秒）
//   bpm      : テンポ
//   division : グリッド分割数（例: 4 = 16分音符）
// ─────────────────────────────────────────────
float Game::SnapToGrid(float time, float bpm, int division)
{
    float secondBeat = 60.0f / bpm;
    float gridUnit = secondBeat / division;
    return std::round(time / gridUnit) * gridUnit;
}

void Game::OnDeviceLost() {}
void Game::OnDeviceRestored()
{
    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}
#pragma endregion
