#pragma once

// The LAN config UI. Runs on its own core-0 task: handleClient() must never
// run on the draw loop, which has about 8 ms of slack in a 66 ms frame.
void portal_begin();
