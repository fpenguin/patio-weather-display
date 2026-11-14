#pragma once
// Poll the physical serial console from task context. No service on the network.
bool display_capture_requested();
void display_capture_start();
