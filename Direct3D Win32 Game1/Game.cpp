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


std::vector<Note> notes;
std::vector<Beams> beams;

std::vector<Note> originalNotes;
static char filename[64] = "chartname";
NoteType currentNoteType = TAP;

int lane;
static float beamLenght = 2.0f;

using namespace DirectX;

using Microsoft::WRL::ComPtr;
auto previoursTime = std::chrono::high_resolution_clock::now();



Game::Game() noexcept(false)
{

    m_deviceResources = std::make_unique<DX::DeviceResources>();
    // TODO: Provide parameters for swapchain format, depth/stencil format, and backbuffer count.
    //   Add DX::DeviceResources::c_AllowTearing to opt-in to variable rate displays.
    //   Add DX::DeviceResources::c_EnableHDR for HDR10 display.
    m_deviceResources->RegisterDeviceNotify(this);
}

Game::~Game()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(HWND window, int width, int height)
{
    sound.Init();



    m_deviceResources->SetWindow(window, width, height);

    m_deviceResources->CreateDeviceResources();
    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();

    // TODO: Change the timer settings if you want something other than the default variable timestep mode.
    // e.g. for 60 FPS fixed timestep update logic, call:
    /*
    m_timer.SetFixedTimeStep(true);
    m_timer.SetTargetElapsedSeconds(1.0 / 60);
    */
    //  バージョンの確認
    IMGUI_CHECKVERSION();

    //  コンテキストの作成
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // キーボードによるナビゲーションの有効化
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // コントローラーによるナビゲーションの有効化

    //  Win32用の初期化
    ImGui_ImplWin32_Init(window);
    //  DirectX11用の初期化
    ID3D11Device* device = m_deviceResources->GetD3DDevice();
    ID3D11DeviceContext* context = m_deviceResources->GetD3DDeviceContext();
    ImGui_ImplDX11_Init(device, context);
}

#pragma region Frame Update
// Executes the basic game loop.
void Game::Tick()
{
    //  新フレームの開始（メインループの一番上に記述）
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    //  デモウィンドウの描画
    //ImGui::ShowDemoWindow();
    m_timer.Tick([&]()
        {
            Update(m_timer);
        });

    Render();
}

// Updates the world.
void Game::Update(DX::StepTimer const& timer)
{
    if (GetAsyncKeyState(VK_LBUTTON) & 0x0001)
    {
        mousePressed = true;
    }
    else
    {
        mousePressed = false;
    }
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
    {
        mouseDown = true;
    }
    else
    {
        mouseDown = false;
    }



    float elapsedTime = float(timer.GetElapsedSeconds());
    //  新しいウィンドウの作成
    //ImGui::Begin("TestWindow");
    //{
    //    //  テキストの表示
    //    ImGui::Text("Hello, world!!");
    //}
    //ImGui::End();
    //ノーツを置いてもいいかの確認
    if (GetAsyncKeyState('Q') & 0x0001)
    {
        oknort = true;
    }
    if (GetAsyncKeyState('W') & 0x0001 && oknort == true)
    {
        oknort = false;
    }
    //ミラー切り替え
    if (GetAsyncKeyState('Z') & 0x0001)
    {
        mirrorMode = true;
    }
    if (GetAsyncKeyState('X') & 0x0001 && mirrorMode == true)
    {
        mirrorMode = false;
    }
    ImGui::SliderFloat("Long Interval", &LongInterval, 0.03f, 0.5f);
    if (LongStartPlaced)
    {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), ">>> START placed! Click END position <<<");
    }

    if (oknort == false)
    {
        if (mousePressed && currentNoteType != LONG_START)
        {
            if (lane >= 0 && lane < LANE_COUNT)
            {
                NoteBorn();
            }
        }
        if (lane >= 0 && lane < LANE_COUNT)
        {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(GetActiveWindow(), &p);

            float longInterval = 0.15f; // CENTERの間隔
            float mouseX = (float)p.x;
            float mouseY = (float)p.y - 30.0f;

            // BPM情報
            float secondsPerBeat = 60.0f / bpm;
            // pixelsPerBeat はグリッドの1拍の高さ
            float pixelsPerBeat = 100.0f;
            // クリック位置からレーンを計算
            int clickedLane = (int)((mouseX - laneStartX) / laneWidth);
            if (clickedLane < 0) clickedLane = 0;
            if (clickedLane >= LANE_COUNT) clickedLane = LANE_COUNT - 1;

            float clickedBeat = currentBeat + (judgeLine - mouseY) / pixelsPerBeat;
            float clickedTime = clickedBeat * secondsPerBeat;
            clickedTime = SnapToGrid(clickedTime, bpm, division);
            // ===== STARTノーツ =====
            if (mousePressed && currentNoteType == LONG_START)
            {
                if (!LongStartPlaced)
                {
                    notes.push_back(Note(clickedLane, clickedTime, LONG_START));
                    LongStartPlaced = true;
                    LongStartLane = clickedLane;
                    LongStartTime = clickedTime;
                }
                else
                {
                    //2クリック目　CENTER自動生成＋END配置
                    float startT = LongStartTime;
                    float endT = clickedTime;
                    clickedTime = SnapToGrid(clickedTime, bpm, division);
                    if (endT > startT)
                    {
                        float t = startT + LongInterval;
                        while (t < endT - LongInterval * 0.5f)
                        {
                            notes.push_back(Note(LongStartLane, t, LONG_CENTER));
                            t += LongInterval;
                        }
                        notes.push_back(Note(LongStartLane, endT, LONG_END));

                        std::sort(notes.begin(), notes.end(),
                            [](const Note& a, const Note& b)
                            { return a.time < b.time; });
                    }
                    LongStartPlaced = false;
                }
            }
        }
    }



    //判定群
    JudgeNoteS();
    JudgeNoteD();
    JudgeNoteF();
    JudgeNoteJ();
    JudgeNoteK();
    JudgeNoteL();
    JudgeLongNoteS();
    JudgeLongNoteD();
    JudgeLongNoteF();
    JudgeLongNoteJ();
    JudgeLongNoteK();
    JudgeLongNoteL();


    if (GetAsyncKeyState(VK_LEFT) & 0x0001)
    {
        mirrorLane--;
    }
    if (GetAsyncKeyState(VK_RIGHT) & 0x0001)
    {
        mirrorLane++;
    }
    //範囲制限
    if (mirrorLane < 0) mirrorLane = 0;
    if (mirrorLane >= LANE_COUNT) mirrorLane = LANE_COUNT;

    if (GetAsyncKeyState(VK_RBUTTON) & 0x0001)
    {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(GetActiveWindow(), &p);

        float mouseX = (float)p.x;
        float mouseY = (float)p.y - 20.0f;

        for (int i = 0; i < notes.size(); i++)
        {
            float x = laneStartX + notes[i].lane * laneWidth;
            float y = notes[i].time * playSpeed + musicTime;

            if (mouseX >= x && mouseX <= x + laneWidth &&
                mouseY >= y && mouseY <= y + noteHeight)
            {
                notes.erase(notes.begin() + i);
                break;
            }
        }
    }

    scroll += ImGui::GetIO().MouseWheel * 40.0f;


    static bool KeySpacePrev = false;

    bool keySpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

    if (keySpace && !KeySpacePrev) // 押下時のみ
    {
        switch (bgmstate)
        {
        case BGMstate::stop:
            //保存
            originalNotes = notes;
            sound.Play(SOUND_LABEL_BGM001); // 先頭から再生
            bgmstate = BGMstate::play;
            break;

        case BGMstate::play:
            // ノーツを元に戻す
            notes = originalNotes;
            success = 0;
            miss = 0;
            sound.Pause(SOUND_LABEL_BGM001); // 停止（途中再生可能）
            bgmstate = BGMstate::pause;
            break;

        case BGMstate::pause:
            sound.Resume(SOUND_LABEL_BGM001); // 途中から再生
            bgmstate = BGMstate::play;
            if (mirrorMode == true)
            {
                mirrorMode = !mirrorMode;
            }
            break;
        }
    }

    if (bgmstate == BGMstate::play)
    {
        musicTime += elapsedTime;
    }
    else
    {
        float wheel = ImGui::GetIO().MouseWheel;
        editorBeat += wheel;
        if (GetAsyncKeyState(VK_UP) & 0x8000)
        {
            editorBeat += 0.1f;
        }
        if (GetAsyncKeyState(VK_DOWN) & 0x8000)
        {
            editorBeat -= 0.1f;
        }
    }
    // 前フレーム状態を更新
    KeySpacePrev = keySpace;


    if (bgmstate == BGMstate::play)
    {
        for (int i = 0; i < notes.size(); i++)
        {
            float diff = musicTime - notes[i].time;

            if (diff > 0.2f)
            {
                printf("MISS\n");
                notes.erase(notes.begin() + i);
                miss++;
                i--;
            }
        }
    }

    // TODO: Add your game logic here.
    elapsedTime;
}
#pragma endregion

#pragma region Frame Render
// Draws the scene.
void Game::Render()
{
    if (m_timer.GetFrameCount() == 0)
    {
        return;
    }

    Clear();

    m_deviceResources->PIXBeginEvent(L"Render");
    auto context = m_deviceResources->GetD3DDeviceContext();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

   // ImGui::Begin("Editor",
   //     nullptr,
   //     ImGuiWindowFlags_NoMove |
   //     ImGuiWindowFlags_NoResize |
   //     ImGuiWindowFlags_NoCollapse |
   //     ImGuiWindowFlags_NoNav
   // );

   // ImDrawList* draw = ImGui::GetWindowDrawList();
   // ImVec2 pos = ImGui::GetCursorScreenPos();



   // // レーン描画
   // for (int i = 0; i <= LANE_COUNT; i++)
   // {
   //     float x = pos.x + laneStartX + laneWidth * i;

   //     draw->AddLine(
   //         ImVec2(x, pos.y),
   //         ImVec2(x, pos.y + 600),
   //         IM_COL32(255, 255, 255, 255),
   //         2.0f
   //     );
   // }
   // //ここで長さを変更
   //// float gridHeight = -4000.0f;

   // //グリッド
   // bpm = 120;
   // //ここをいじったら曲の速さ変わるよ
   // float pixelsPerBeat = 100.0f;
   // float secondsPerBeat = 60.0f / bpm;
   // float musicBeat = musicTime / secondsPerBeat; // 今何拍目か
   // if (bgmstate == BGMstate::play)
   // {
   //     currentBeat = musicTime / secondsPerBeat;
   // }
   // else
   // {
   //     currentBeat = editorBeat; // 手動スクロール用
   // }
   // for (int step = 0; step < totalSteps; step++)
   // {
   //     float timeStep = step * (1.0f / division); // 1拍をdivisionで分割
   //     float lineY = pos.y + judgeLine - (timeStep - currentBeat) * pixelsPerBeat;
   //     ImU32 color;
   //     if (step % (beatsPerMeasure * division) == 0)
   //         color = IM_COL32(255, 255, 255, 255);   //小節
   //     else if (step % division == 0)
   //         color = IM_COL32(200, 200, 200, 255);   //拍
   //     else
   //         color = IM_COL32(100, 100, 100, 255);   //16分

   //     draw->AddLine(
   //         ImVec2(pos.x + laneStartX, lineY),
   //         ImVec2(pos.x + laneStartX + laneWidth * LANE_COUNT, lineY),
   //         color
   //     );

   //     if (step % (beatsPerMeasure * division) == 0)
   //     {
   //         int meausureNow = step / (beatsPerMeasure * division) + 1;
   //         draw->AddText
   //         (ImVec2(pos.x + 5, lineY - 10),
   //             IM_COL32(255, 255, 0, 255),
   //             std::to_string(meausureNow).c_str()
   //         );
   //     }
   // }

   // //判定表示=============================================
   // draw->AddText
   // (ImVec2(pos.x + 700, pos.y + 150),
   //     IM_COL32(255, 0, 255, 255),
   //     std::to_string(success).c_str()
   // );

   // draw->AddText
   // (ImVec2(pos.x + 700, pos.y + 200),
   //     IM_COL32(20, 255, 255, 255),
   //     std::to_string(miss).c_str()
   // );
   // //===================================================

   // draw->AddLine(
   //     ImVec2(pos.x + laneStartX, pos.y + judgeLine),
   //     ImVec2(pos.x + laneStartX + laneWidth * LANE_COUNT, pos.y + judgeLine),
   //     IM_COL32(255, 0, 0, 255),
   //     4.0f
   // );

   // // ノーツ描画
   // for (auto& note : notes)
   // {
   //     ID3D11ShaderResourceView* texture = nullptr;

   //     switch (note.note)
   //     {
   //     case TAP:
   //         texture = noteTap;
   //         noteHeight = 20.0f;
   //         break;

   //     case LONG_START:
   //         texture = noteLongStart;
   //         noteHeight = 20.0f;
   //         break;

   //     case LONG_CENTER:
   //         texture = noteLongCenter;
   //         noteHeight = 20.0f;
   //         break;

   //     case LONG_END:
   //         texture = noteLongEnd;
   //         noteHeight = 20.0f;
   //         break;

   //     case UNTIMIRROR:
   //         texture = noteUntiMirror;
   //         noteHeight = 20.0f;
   //         break;

   //     case MIRRORNOTE:
   //         texture = noteMirror;
   //         noteHeight = 20.0f;
   //         break;

   //     case BEAM:
   //         texture = Beam;
   //         noteHeight = -300.0f;
   //         break;

   //     default:
   //         texture = noteTap;
   //         break;
   //     }

   //     //ここから下反転処理====================================================================================
   //     int drawLane = note.lane;

   //     if (mirrorMode && mirrorLane == 5 && note.note != UNTIMIRROR)
   //     {
   //         if (note.lane == 4)
   //         {
   //             drawLane = 5;
   //         }
   //         else if (note.lane == 5)
   //         {
   //             drawLane = 4;
   //         }
   //     }
   //     if (mirrorMode && mirrorLane == 4 && note.note != UNTIMIRROR)
   //     {
   //         if (note.lane == 3)
   //         {
   //             drawLane = 4;
   //         }
   //         else if (note.lane == 4)
   //         {
   //             drawLane = 3;
   //         }
   //         if (note.lane == 2)
   //         {
   //             drawLane = 5;
   //         }
   //         else if (note.lane == 5)
   //         {
   //             drawLane = 2;
   //         }
   //     }
   //     if (mirrorMode && mirrorLane == 3 && note.note != UNTIMIRROR)
   //     {
   //         if (note.lane == 0)
   //         {
   //             drawLane = 5;
   //         }
   //         else if (note.lane == 5)
   //         {
   //             drawLane = 0;
   //         }
   //         if (note.lane == 1)
   //         {
   //             drawLane = 4;
   //         }
   //         else if (note.lane == 4)
   //         {
   //             drawLane = 1;
   //         }
   //         if (note.lane == 2)
   //         {
   //             drawLane = 3;
   //         }
   //         else if (note.lane == 3)
   //         {
   //             drawLane = 2;
   //         }
   //     }
   //     if (mirrorMode && mirrorLane == 2 && note.note != UNTIMIRROR)
   //     {
   //         if (note.lane == 0)
   //         {
   //             drawLane = 3;
   //         }
   //         else if (note.lane == 3)
   //         {
   //             drawLane = 0;
   //         }
   //         if (note.lane == 1)
   //         {
   //             drawLane = 2;
   //         }
   //         else if (note.lane == 2)
   //         {
   //             drawLane = 1;
   //         }
   //     }
   //     if (mirrorMode && mirrorLane == 1 && note.note != UNTIMIRROR)
   //     {
   //         if (note.lane == 0)
   //         {
   //             drawLane = 1;
   //         }
   //         else if (note.lane == 1)
   //         {
   //             drawLane = 0;
   //         }
   //     }
   //     //===============================================================================================
   //     float x = pos.x + laneStartX + drawLane * laneWidth;
   //     float noteBeat = note.time / secondsPerBeat;
   //     float y = pos.y + judgeLine - (noteBeat - currentBeat) * pixelsPerBeat;
   //     draw->AddImage(
   //         texture,
   //         ImVec2(x, y),
   //         //ここでノーツごとの大きさを固定化
   //         ImVec2(x + laneWidth, y + noteHeight)
   //     );
   // }

   // // ビーム描画
   // for (const auto& beam : beams)
   // {
   //     float currentTime = musicTime; // 現在の再生時刻

   //     // まだ予告も始まってない
   //     if (currentTime < beam.warningTime) continue;

   //     // 終了済み
   //     if (currentTime > beam.endTime) continue;

   //     float x = pos.x + laneStartX + beam.lane * laneWidth;
   //     float beamTop = pos.y;      // 画面上端
   //     float beamBottom = pos.y + 600; // 画面下端（レーンの長さ）

   //     if (currentTime >= beam.startTime && currentTime <= beam.endTime)
   //     {
   //         // 本ビーム（赤など目立つ色）
   //         draw->AddRectFilled(
   //             ImVec2(x, beamTop),
   //             ImVec2(x + laneWidth, beamBottom),
   //             IM_COL32(255, 0, 0, 180)
   //         );
   //     }
   //     else if (currentTime >= beam.warningTime && currentTime < beam.startTime)
   //     {
   //         // 予告線（薄い色や点線風）
   //         draw->AddRectFilled(
   //             ImVec2(x, beamTop),
   //             ImVec2(x + laneWidth, beamBottom),
   //             IM_COL32(255, 255, 0, 80)
   //         );
   //     }
   // }


   // float mirrorX = pos.x + laneStartX + mirrorLane * laneWidth;

   // draw->AddLine(
   //     ImVec2(mirrorX, pos.y),
   //     ImVec2(mirrorX, pos.y + 600),
   //     IM_COL32(255, 0, 0, 255),
   //     3.0f
   // );

   // //ImGui::SetCursorPos(ImVec2(0, 500));
   // ImGui::Text("Note Type");
   // if (ImGui::Button("TAP") || GetAsyncKeyState('1') & 0x0001)
   // {
   //     currentNoteType = TAP;
   // }
   // ImGui::SameLine();

   // if (ImGui::Button("Long") || GetAsyncKeyState('2') & 0x0001)
   // {
   //     currentNoteType = LONG_START;
   // }

   // ImGui::SameLine();

   // if (ImGui::Button("AntiMirror") || GetAsyncKeyState('3') & 0x0001)
   // {
   //     currentNoteType = UNTIMIRROR;
   // }
   // ImGui::SameLine();

   // if (ImGui::Button("MirrorNote") || GetAsyncKeyState('4') & 0x0001)
   // {
   //     currentNoteType = MIRRORNOTE;
   // }

   // if (ImGui::Button("Beam") || GetAsyncKeyState('5') & 0x0001)
   // {
   //     currentNoteType = BEAM;
   // }
   // //ImGui::SameLine();

   // ImGui::InputText("Filename", filename, 64);
   // if (ImGui::Button("Save"))
   // {
   //     //保存前にmirrorModeがtrueの場合falseに戻します
   //     if (mirrorMode) mirrorMode = false;
   //     SaveChart(filename);
   // }
   // if (ImGui::Button("Load"))
   // {
   //     LoadChart(filename);
   // }

   // static float beamStartInput = 0.0f;
   // static float beamEndInput = 1.0f;
   // static float beamWarningOffset = 0.5f; // startTimeより何秒前に予告を出すか
   // static int beamLaneInput = 0;

   // ImGui::Separator();
   // ImGui::Text("Beam Settings");
   // ImGui::SliderInt("Beam Lane", &beamLaneInput, 0, LANE_COUNT - 1);
   // ImGui::InputFloat("Start Time", &beamStartInput, 0.1f);
   // ImGui::InputFloat("End Time", &beamEndInput, 0.1f);
   // ImGui::InputFloat("Warning Time", &beamWarningOffset, 0.1f);
   // ImGui::SliderInt("Division", &division, 1, 16);

   // if (ImGui::Button("Add Beam"))
   // {
   //     float warningTime = beamStartInput - beamWarningOffset;
   //     if (warningTime < 0) warningTime = 0;
   //     beams.push_back(Beams(beamLaneInput, beamStartInput, beamEndInput, warningTime));
   // }


   // ImGui::End();

    float screenW = ImGui::GetIO().DisplaySize.x;
    float screenH = ImGui::GetIO().DisplaySize.y;
    float leftW = 200.0f;
    float bottomH = 60.0f;

    //左
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(leftW, screenH - bottomH));
    ImGui::Begin("LeftPanel", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    //左パネル======================================================
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(200, screenH - bottomH));
    ImGui::Begin("LeftPanel", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    //ノーツの種類==================================================
    ImGui::SeparatorText("Note Type");

    //選択中のノーツを色付き表示
    const char* noteNames[] = { "TAP","LONG","antiMirror","MirrorNote","Beam" };
    NoteType noteTypes[] = { TAP, LONG_START, UNTIMIRROR, MIRRORNOTE, BEAM };

    for (int i = 0; i < 5; i++)
    {
        bool selected = (currentNoteType == noteTypes[i]);
        if (selected) 
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));   //色変更
        
        if (ImGui::Button(noteNames[i], ImVec2(-1, 0)))    //幅まで広げる場所
        currentNoteType = noteTypes[i];

        if (selected) 
            ImGui::PopStyleColor();
    }
    //グリッド設定(1小説にどれだけ細かくノーツを置けるかの設定)
    ImGui::SeparatorText("Grid");
    ImGui::SliderInt("Division", &division, 1, 16);
    //ビーム設定
    ImGui::SeparatorText("Beam");
    static float beamStartInput = 0.0f;     //ビーム開始時間
    static float beamEndInput = 1.0f;       //ビーム終わり時間
    static float beamWarningOffset = 0.5f;  //ビーム発射前の忠告線が何秒前から出るか
    static int beamLaneInput = 0;           //ビームを出すレーン

    ImGui::SliderInt("Lane##beam", &beamLaneInput, 0, LANE_COUNT - 1);
    ImGui::InputFloat("Start##beam", &beamStartInput, 0.1f);
    ImGui::InputFloat("End##beam", &beamEndInput, 0.1f);
    ImGui::InputFloat("Warning##beam", &beamWarningOffset, 0.1f);

    if (ImGui::Button("Add Beam", ImVec2(-1, 0)))
    {
        float warningTime = beamStartInput - beamWarningOffset;
        //もし発車時間よりも忠告線の方が速いもしくは差が0なら忠告時間を0に変更
        if (warningTime < 0) warningTime = 0;
        beams.push_back(Beams(beamLaneInput, beamStartInput, beamEndInput, warningTime));
    }
    //ファイル保存=============================================
    ImGui::SeparatorText("Fire");
    ImGui::InputText("##filename", filename, 64);   //##をつけることでfilenameを消す
    if (ImGui::Button("Save", ImVec2(90, 0)));
    {
        //もし保存時にミラーモードがONならOFFに切り替えて保存
        if (mirrorMode) mirrorMode = false;
        SaveChart(filename);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load", ImVec2(90, 0)))
    {
        //ファイル読み込み
        LoadChart(filename);
    }
    ImGui::End();
    ImGui::End();
    //譜面エリア
    ImGui::SetNextWindowPos(ImVec2(leftW, 0));
    ImGui::SetNextWindowSize(ImVec2(screenW - leftW, screenH - bottomH));
    ImGui::Begin("Editor", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    //draw->AddLineとか
    ImGui::End();

    //下のバー
    ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomH));
    ImGui::SetNextWindowSize(ImVec2(screenW, bottomH));
    ImGui::Begin("BottomBar", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar
    );
    // 再生・停止・もどるボタン、時間表示
    ImGui::End();

    context;

    m_deviceResources->PIXEndEvent();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    m_deviceResources->Present();
}

// Helper method to clear the back buffers.
void Game::Clear()
{
    m_deviceResources->PIXBeginEvent(L"Clear");

    // Clear the views.
    auto context = m_deviceResources->GetD3DDeviceContext();
    auto renderTarget = m_deviceResources->GetRenderTargetView();
    auto depthStencil = m_deviceResources->GetDepthStencilView();

    context->ClearRenderTargetView(renderTarget, Colors::CornflowerBlue);
    context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    context->OMSetRenderTargets(1, &renderTarget, depthStencil);

    // Set the viewport.
    auto const viewport = m_deviceResources->GetScreenViewport();
    context->RSSetViewports(1, &viewport);

    m_deviceResources->PIXEndEvent();
}
#pragma endregion

#pragma region Message Handlers
// Message handlers
void Game::OnActivated()
{
    // TODO: Game is becoming active window.
}

void Game::OnDeactivated()
{
    // TODO: Game is becoming background window.
}

void Game::OnSuspending()
{
    // TODO: Game is being power-suspended (or minimized).
}

void Game::OnResuming()
{
    m_timer.ResetElapsedTime();

    // TODO: Game is being power-resumed (or returning from minimize).
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
    if (!m_deviceResources->WindowSizeChanged(width, height))
        return;

    CreateWindowSizeDependentResources();

    // TODO: Game window is being resized.
}

void Game::SaveChart(std::string name)
{
    std::ofstream file("charts/" + name + ".json");

    file << "{\n";
    file << "\"notes\": [\n";

    for (size_t i = 0; i < notes.size(); i++)
    {
        file << "{";
        file << "\"lane\": " << notes[i].lane << ",";
        file << "\"type\": " << notes[i].note << ",";
        file << "\"time\": " << notes[i].time;
        file << "}";

        if (i != notes.size() - 1)
            file << ",";

        file << "\n";
    }

    file << "],\n";

    //ビームの保存
    file << "\"beams\": [\n";
    for (size_t i = 0; i < beams.size(); i++)
    {
        file << "{";
        file << "\"lane\": " << beams[i].lane << ",";
        file << "\"startTime\": " << beams[i].startTime << ",";
        file << "\"endTime\": " << beams[i].endTime << ",";
        file << "\"warningTime\": " << beams[i].warningTime;
        file << "}";
        if (i != beams.size() - 1) file << ",";
        file << "\n";
    }
    file << "]\n";

    file << "}\n";
}

void Game::LoadChart(std::string name)
{
    std::ifstream file("charts/" + name + ".json");

    if (!file.is_open()) return;

    notes.clear();
    beams.clear();

    std::string line;

    while (std::getline(file, line))
    {
        int lane;
        int type;
        float time;

        if (sscanf_s(line.c_str(),
            "{\"lane\": %d,\"type\": %d,\"time\": %f}",
            &lane, &type, &time) == 3)
        {
            notes.push_back(Note(lane, time, (NoteType)type));



            std::sort(notes.begin(), notes.end(),
                [](const Note& a, const Note& b)
                {
                    return a.time < b.time;
                });
        }
        // ビーム読み込み
        float startTime, endTime, warningTime;
        if (sscanf_s(line.c_str(),
            "{\"lane\": %d,\"startTime\": %f,\"endTime\": %f,\"warningTime\": %f}",
            &lane, &startTime, &endTime, &warningTime) == 4)
        {
            beams.push_back(Beams(lane, startTime, endTime, warningTime));

            std::sort(beams.begin(), beams.end(),
                [](const Beams& a, const Beams& b)
                {
                    return a.time < b.time;
                });
        }
    }
}

void Game::NoteBorn()
{
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(GetActiveWindow(), &p);

    float mouseX = (float)p.x;
    float mouseY = (float)p.y - 30.0f;

    // クリック位置からレーンを計算
    int clickedLane = (int)((mouseX - laneStartX) / laneWidth);
    if (clickedLane < 0)
        clickedLane = 0;
    else if (clickedLane >= LANE_COUNT)
        clickedLane = LANE_COUNT - 1;

    // BPM情報
    float secondsPerBeat = 60.0f / bpm;

    // pixelsPerBeat はグリッドの1拍の高さ
    float pixelsPerBeat = 100.0f;


    // currentBeat を使う（再生中はmusicTime換算、停止中はeditorBeat）
    float clickedBeat = currentBeat + (judgeLine - mouseY) / pixelsPerBeat;
    float clickedTime = clickedBeat * secondsPerBeat;
    clickedTime = SnapToGrid(clickedTime, bpm, division);
    // ノーツ追加
    notes.push_back(Note(clickedLane, clickedTime, currentNoteType));

}

void Game::JudgeNoteS()
{
    //判定
    if (GetAsyncKeyState('S') & 0x0001)
    {
        int targetLane;
        if (!mirrorMode)
        {
            targetLane = 0; //Sの場合
        }
        else
        {
            targetLane = 5;
        }


        float closeNote = 9999.0f;  //レーンから一番近いノーツを探索
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //ターゲットレーン以外のノーツをスルー
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONGをスルー

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;

                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }

            }
        }
    }
}

void Game::JudgeNoteD()
{
    //判定
    if (GetAsyncKeyState('D') & 0x0001)
    {
        int targetLane; //Dの場合
        if (!mirrorMode)
        {
            targetLane = 1;
        }
        else
        {
            targetLane = 4;
        }

        float closeNote = 9999.0f;  //レーンから一番近いノーツを探索
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //ターゲットレーン以外のノーツをスルー
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONGをスルー

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;
                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
            }
        }
    }
}

void Game::JudgeNoteF()
{
    //判定
    if (GetAsyncKeyState('F') & 0x0001)
    {
        int targetLane; //Fの場合
        if (!mirrorMode)
        {
            targetLane = 2;
        }
        else
        {
            targetLane = 3;
        }

        float closeNote = 9999.0f;  //レーンから一番近いノーツを探索
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //ターゲットレーン以外のノーツをスルー
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONGをスルー

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;
                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
            }
        }
    }
}

void Game::JudgeNoteJ()
{
    //判定
    if (GetAsyncKeyState('J') & 0x0001)
    {
        int targetLane; //Jの場合
        if (!mirrorMode)
        {
            targetLane = 3;
        }
        else
        {
            targetLane = 2;
        }

        float closeNote = 9999.0f;  //レーンから一番近いノーツを探索
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //ターゲットレーン以外のノーツをスルー
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONGをスルー

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;
                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
            }
        }
    }
}

void Game::JudgeNoteK()
{
    //判定
    if (GetAsyncKeyState('K') & 0x0001)
    {
        int targetLane; //Kの場合
        if (!mirrorMode)
        {
            targetLane = 4;
        }
        else
        {
            targetLane = 1;
        }

        float closeNote = 9999.0f;  //レーンから一番近いノーツを探索
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //ターゲットレーン以外のノーツをスルー
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONGをスルー

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;
                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
            }
        }
    }
}

void Game::JudgeNoteL()
{
    //判定
    if (GetAsyncKeyState('L') & 0x0001)
    {
        int targetLane; //Lの場合
        if (!mirrorMode)
        {
            targetLane = 5;
        }
        else
        {
            targetLane = 0;
        }

        float closeNote = 9999.0f;  //レーンから一番近いノーツを探索
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //ターゲットレーン以外のノーツをスルー
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONGをスルー

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;
                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
            }
        }
    }
}


void Game::JudgeLongNoteS()
{
    int targetLane;
    if (!mirrorMode)
    {
        targetLane = 0; //Sの場合
    }
    else
    {
        targetLane = 5;
    }

    bool keyPress = (GetAsyncKeyState('S') & 0x8000);
    bool keyPush = (GetAsyncKeyState('S') & 0x0001);
    static bool prevKeyDown = false;

    // ロング状態
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // 判定ラインを超えたCENTERは消す
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

void Game::JudgeLongNoteD()
{
    int targetLane; //Dの場合
    if (!mirrorMode)
    {
        targetLane = 1;
    }
    else
    {
        targetLane = 4;
    }

    bool keyPress = (GetAsyncKeyState('D') & 0x8000);
    bool keyPush = (GetAsyncKeyState('D') & 0x0001);
    static bool prevKeyDown = false;

    // ロング状態
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // 判定ラインを超えたCENTERは消す
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

void Game::JudgeLongNoteF()
{
    int targetLane; //Fの場合
    if (!mirrorMode)
    {
        targetLane = 2;
    }
    else
    {
        targetLane = 3;
    }

    bool keyPress = (GetAsyncKeyState('F') & 0x8000);
    bool keyPush = (GetAsyncKeyState('F') & 0x0001);
    static bool prevKeyDown = false;

    // ロング状態
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // 判定ラインを超えたCENTERは消す
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

void Game::JudgeLongNoteJ()
{
    int targetLane; //Jの場合
    if (!mirrorMode)
    {
        targetLane = 3;
    }
    else
    {
        targetLane = 2;
    }

    bool keyPress = (GetAsyncKeyState('J') & 0x8000);
    bool keyPush = (GetAsyncKeyState('J') & 0x0001);
    static bool prevKeyDown = false;

    // ロング状態
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // 判定ラインを超えたCENTERは消す
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

void Game::JudgeLongNoteK()
{
    int targetLane; //Kの場合
    if (!mirrorMode)
    {
        targetLane = 4;
    }
    else
    {
        targetLane = 1;
    }

    bool keyPress = (GetAsyncKeyState('K') & 0x8000);
    bool keyPush = (GetAsyncKeyState('K') & 0x0001);
    static bool prevKeyDown = false;

    // ロング状態
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // 判定ラインを超えたCENTERは消す
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

void Game::JudgeLongNoteL()
{
    int targetLane; //Lの場合
    if (!mirrorMode)
    {
        targetLane = 5;
    }
    else
    {
        targetLane = 0;
    }

    bool keyPress = (GetAsyncKeyState('L') & 0x8000);
    bool keyPush = (GetAsyncKeyState('L') & 0x0001);
    static bool prevKeyDown = false;

    // ロング状態
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // 判定ラインを超えたCENTERは消す
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

bool Game::NoteExit(int lane, float time)
{
    for (const auto& n : notes)
    {
        if (n.lane == lane && fabs(n.time - time) < 0.01f)
        {
            return true;
        }
    }
    return false;
}

int Game::GetMirrorLane(int lane)
{
    if (!mirrorMode) return lane;
    if (mirrorLane == 3)
    {

    }

    //if (mirrored < 0 || mirrored >= LANE_COUNT)
    //{
    //    return -1;
    //}
    //return mirrored;
}

// Properties
void Game::GetDefaultSize(int& width, int& height) const noexcept
{
    // TODO: Change to desired default window size (note minimum size is 320x200).
    width = 800;
    height = 600;
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();

    CreateWICTextureFromFile(
        device,
        L"asset/hato_touka.png",
        nullptr,
        &noteTexture
    );
    //タップ
    CreateWICTextureFromFile(
        device,
        L"asset/hato_touka.png",
        nullptr,
        &noteTap
    );
    //ロング
    CreateWICTextureFromFile(
        device,
        L"asset/ude.png",
        nullptr,
        &noteLongStart
    );

    CreateWICTextureFromFile(
        device,
        L"asset/Long(aida).png",
        nullptr,
        &noteLongCenter
    );

    CreateWICTextureFromFile(
        device,
        L"asset/Long(kari).jpg",
        nullptr,
        &noteLongEnd
    );
    //アンチミラー
    CreateWICTextureFromFile(
        device,
        L"asset/Untimirror(kari).jpg",
        nullptr,
        &noteUntiMirror
    );
    //ミラーノーツ（たたいたらミラーオンオフ切り替え）
    CreateWICTextureFromFile(
        device,
        L"asset/block.png",
        nullptr,
        &noteMirror
    );
    //ビーム（予告線とかも兼ねて）
    CreateWICTextureFromFile(
        device,
        L"asset/beam.png",
        nullptr,
        &Beam
    );
}

// Allocate all memory resources that change on a window SizeChanged event.
void Game::CreateWindowSizeDependentResources()
{
    // TODO: Initialize windows-size dependent objects here.
}

float Game::SnapToGrid(float time, float bpm, int division)
{
    float secondBeat = 60.0f / bpm;
    float gridUnit = secondBeat / division; //1グリッドの秒数

    return std::round(time / gridUnit) * gridUnit;
}

void Game::OnDeviceLost()
{
    // TODO: Add Direct3D resource cleanup here.
}

void Game::OnDeviceRestored()
{
    CreateDeviceDependentResources();

    CreateWindowSizeDependentResources();
}
#pragma endregion
