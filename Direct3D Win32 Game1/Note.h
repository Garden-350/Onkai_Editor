#pragma once
enum NoteType
{
    TAP,
    LONG_START,
    LONG_CENTER,
    LONG_END,
    UNTIMIRROR, //ミラーの影響を受けないノーツ
    MIRRORNOTE, //叩いたらミラーオンオフが切り替わる
    BEAM,
};

struct Note
{
public:
    int lane;   // レーン番号
    float y;    // Y座標
    float time;
    float startLong;
    float endLong;
    float lenght;   //ビーム用
    NoteType note;

    Note();
    Note(int lane, float t, NoteType note);

    float GetTime() const { return time; }  // getter
};

//NoteType currentNoteType = TAP;