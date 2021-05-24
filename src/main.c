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

#define DEBOUNCE_DELAY 300

#define degToRad(angleInDegrees) ((angleInDegrees) * M_PI / 180.0)

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
#define buttons_DEBOUNCE_DELAY	( 200 / portTICK_RATE_MS )

// Stores last time when a button was pressed
TickType_t buttons_last_change;

// Counters for keystrokers 
int count_A, count_B, count_C, count_D;


/* Prevents buttons from bouncing:
 * returns 1 if debouncing has been achieved,
 * and 0 if it can't be achieved */
uint8_t DebouncingCondition (void) {

    const int buttons_change_period = buttons_DEBOUNCE_DELAY;

    if (xTaskGetTickCount() - buttons_last_change > buttons_change_period) {
        buttons_last_change = xTaskGetTickCount();
        return 1;
    } else {
        return 0;
    }
}

/* Counts how many times A, B, C and D have been pushed,
 * then prints the output on the screen */
void vDrawButtonText(void) {

    static char str[100] = { 0 };

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {

        if (buttons.buttons[KEYCODE(A)] && DebouncingCondition()) 
            count_A++;
        if (buttons.buttons[KEYCODE(B)] && DebouncingCondition())
            count_B++;
        if (buttons.buttons[KEYCODE(C)] && DebouncingCondition())
            count_C++;
        if (buttons.buttons[KEYCODE(D)] && DebouncingCondition())
            count_D++;
        
        sprintf(str, "A: %d | B: %d | C: %d | D: %d",
            count_A, count_B, count_C, count_D);

        xSemaphoreGive(buttons.lock);

        tumDrawText(str, 10, DEFAULT_FONT_SIZE, Black);
    }

}

// Draws a triangle at given coordinates
void putTriangleAt(coord_t * points) {

    // Triangle size
    const short triangle_half_base = 64;
    const short triangle_height = 80;

    coord_t coord_triangle[3]= {
            {points->x - triangle_half_base, points->y + triangle_height/2},
            {points->x, points->y -triangle_height/2},
            {points->x + triangle_half_base, points->y + triangle_height/2},
    };

    tumDrawTriangle(coord_triangle,Black);   

}

// Draws a circle at given coordinates
void putCircleAt(coord_t * points) {
    // Circle diameter
    const short diameter = 45;

    tumDrawCircle(points->x, points->y, diameter, Red);

}

// Draws a squre at given coordingates
void putSquareAt(coord_t * points) {

    // Square size
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

    uint8_t firstRun = 0;

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

        vDrawButtonText(); // Print keystrokes

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
        if (!firstRun)
            vTaskDelay((TickType_t)500);

        vTaskDelay((TickType_t)50);
        firstRun = 1;
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
