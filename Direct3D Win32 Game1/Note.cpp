#include "pch.h"
#include "Note.h"

Note::Note()
{
    lane = 0;
    y = 0.0f;
    time = 0.0f;
    lenght = 1.0f;
}

//Note::Note(int lane, float t, NoteType note)
//{
//    this->lane = lane;
//    this->time = t;
//    this->note = note;
//    this->startLong = t;
//    this->lenght = 1.0f;
//    endLong = -1;
//}
