#ifndef APP_H
#define APP_H

// Called once from main() after the CubeMX MX_*_Init() calls. 
void app_init(void);

//Called repeatedly from main()'s while (1) loop.
void app_run_once(void);

#endif /* APP_H */
