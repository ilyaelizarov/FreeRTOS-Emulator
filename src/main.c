#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_Font.h"

#include "AsyncIO.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)
#define degToRad(angleInDegrees) ((angleInDegrees) * M_PI / 180.0)
#define radToDeg(angleInRadians) ((angleInRadians) * 180.0 / M_PI)

static TaskHandle_t DemoTask = NULL;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR

// Draws a trangle with constant size at given coordinates
void putTriangleAt(coord_t * points) {
    // Triangle coordinates
    const short triangle_half_base = 64;
    const short triangle_height = 80;

    coord_t coord_triangle[3]= {
            {points->x - triangle_half_base, points->y + triangle_height/2},
            {points->x, points->y -triangle_height/2},
            {points->x + triangle_half_base, points->y + triangle_height/2},
    };

    tumDrawTriangle(coord_triangle,Black);   

}

// Draws a circle with constant diameter at given coordinates
void putCircleAt(coord_t * points) {
    // Circle diameter
    const short diameter = 80 / 2;

    tumDrawCircle(points->x, points->y, diameter, Red);

}

// Draws a squre with constant side at given coordingates
void putSquareAt(coord_t * points) {
    const short width = 80;
    const short height = width;

    tumDrawFilledBox(points->x-width/2,points->y-height/2,width,height,Blue);
}

void vDemoTask(void *pvParameters)
{
    // Needed such that Gfx library knows which thread controlls drawing
    // Only one thread can call tumDrawUpdateScreen while and thread can call
    // the drawing functions to draw objects. This is a limitation of the SDL
    // backend.
    tumDrawBindThread();

    // Inital coordinates
    coord_t triangle_coord= {
        SCREEN_WIDTH/2,
        SCREEN_HEIGHT/2
    };
    coord_t circle_coord = {
        SCREEN_WIDTH/2 - 120,
        SCREEN_HEIGHT/2
    };
    coord_t square_coord = {
        SCREEN_WIDTH/2 + 120,
        SCREEN_HEIGHT/2
    };

    // Radians
    double deg = 0;

    int flag_task21 = 0;

    while (1) {
        tumEventFetchEvents(FETCH_EVENT_NONBLOCK); // Query events backend for new events, ie. button presses
        xGetButtonInput(); // Update global input

        // `buttons` is a global shared variable and as such needs to be
        // guarded with a mutex, mutex must be obtained before accessing the
        // resource and given back when you're finished. If the mutex is not
        // given back then no other task can access the reseource.
        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
            if (buttons.buttons[KEYCODE(
                                    Q)]) { // Equiv to SDL_SCANCODE_Q
                exit(EXIT_SUCCESS);
            }
            xSemaphoreGive(buttons.lock);
        }

        




        tumDrawClear(White); // Clear screen

        circle_coord.x = SCREEN_WIDTH/2  - 120 * cos(degToRad(deg));
        circle_coord.y = SCREEN_HEIGHT/2 - 120 * sin(degToRad(deg));
        square_coord.x = SCREEN_WIDTH/2  + 120 * cos(degToRad(deg));
        square_coord.y = SCREEN_HEIGHT/2  + 120 * sin(degToRad(deg));

    

        putTriangleAt(&triangle_coord);
        

        
        putCircleAt(&circle_coord);
        putSquareAt(&square_coord);

        // Increment degrees for clockwise motion of the figures
        deg++;


        /*
        // Get the width of the string on the screen so we can center it
        // Returns 0 if width was successfully obtained
        if (!tumGetTextSize((char *)our_time_string,
                            &our_time_strings_width, NULL))
            tumDrawText(our_time_string,
                        SCREEN_WIDTH / 2 -
                        our_time_strings_width / 2,
                        SCREEN_HEIGHT / 2 - DEFAULT_FONT_SIZE / 2,
                        TUMBlue);
        */

        tumDrawUpdateScreen(); // Refresh the screen to draw string

        // Basic sleep of 1000 milliseconds
        if (!flag_task21)
            vTaskDelay((TickType_t)1000);

        vTaskDelay((TickType_t)50);
        flag_task21 = 1;
    }
}

int main(int argc, char *argv[])
{
    char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);

    printf("Initializing: ");

    if (tumDrawInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize drawing");
        goto err_init_drawing;
    }

    if (tumEventInit()) {
        PRINT_ERROR("Failed to initialize events");
        goto err_init_events;
    }

    if (tumSoundInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize audio");
        goto err_init_audio;
    }

    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock");
        goto err_buttons_lock;
    }

    if (xTaskCreate(vDemoTask, "DemoTask", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &DemoTask) != pdPASS) {
        goto err_demotask;
    }

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_demotask:
    vSemaphoreDelete(buttons.lock);
err_buttons_lock:
    tumSoundExit();
err_init_audio:
    tumEventExit();
err_init_events:
    tumDrawExit();
err_init_drawing:
    return EXIT_FAILURE;
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vMainQueueSendPassed(void)
{
    /* This is just an example implementation of the "queue send" trace hook. */
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vApplicationIdleHook(void)
{
#ifdef __GCC_POSIX__
    struct timespec xTimeToSleep, xTimeSlept;
    /* Makes the process more agreeable when using the Posix simulator. */
    xTimeToSleep.tv_sec = 1;
    xTimeToSleep.tv_nsec = 0;
    nanosleep(&xTimeToSleep, &xTimeSlept);
#endif
}
