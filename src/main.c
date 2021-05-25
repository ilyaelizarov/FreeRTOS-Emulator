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


#include "TUM_Draw.h"
#include "TUM_Utils.h"
#include "TUM_Event.h"
#include "TUM_Font.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)1000)

static TaskHandle_t BufferSwap = NULL;

// Handles for the tasks
static TaskHandle_t SwitchingTask1, SwitchingTask2;

/* A protected variable for counting button's pushes
   when we switch between the tasks in exercise 3.2.3 */
typedef struct switchingButtonCounter {
    unsigned int counter;
    SemaphoreHandle_t lock;
} switchingButtonCounter_t;

switchingButtonCounter_t buttonCounter = { 0 };

// Stores last time when a button was pressed
TickType_t buttons_last_change;

// Semaphore to resume vSwitchingTask1 in exercise 3.2.3
SemaphoreHandle_t semSwitchingTask1 = NULL;

// State for the state machine for toggling tasks in exercise 3.2.3
uint8_t toggle_tasks = 0;

/* configSUPPORT_STATIC_ALLOCATION is set to 1, so the application must provide an
implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
used by the Idle task.
This excerpt was taken from https://www.freertos.org/a00110.html without changes */

// Structure that will hold the TCB of the task being created
StaticTask_t xTaskBuffer;
// Buffer that the task being created will use as its stack
StackType_t xStack[ mainGENERIC_STACK_SIZE ];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize ) {
    /* If the buffers to be provided to the Idle task are declared inside this
    function then they must be declared static - otherwise they will be allocated on
    the stack and so not exists after this function exits. */
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
    state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task's stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
    Note that, as the array is necessarily of type StackType_t,
    configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*-----------------------------------------------------------*/

#define FPS_AVERAGE_COUNT 50

void vDrawFPS(void) {
    static unsigned int periods[FPS_AVERAGE_COUNT] = { 0 };
    static unsigned int periods_total = 0;
    static unsigned int index = 0;
    static unsigned int average_count = 0;
    static TickType_t xLastWakeTime = 0, prevWakeTime = 0;
    static char str[10] = { 0 };
    int fps = 0;

    if (average_count < FPS_AVERAGE_COUNT) {
        average_count++;
    }
    else {
        periods_total -= periods[index];
    }

    xLastWakeTime = xTaskGetTickCount();

    if (prevWakeTime != xLastWakeTime) {
        periods[index] =
            configTICK_RATE_HZ / (xLastWakeTime - prevWakeTime);
        prevWakeTime = xLastWakeTime;
    }
    else {
        periods[index] = 0;
    }

    periods_total += periods[index];

    if (index == (FPS_AVERAGE_COUNT - 1)) {
        index = 0;
    }
    else {
        index++;
    }

    fps = periods_total / average_count;

    sprintf(str, "FPS: %2d", fps);

    tumDrawText(str, SCREEN_WIDTH/2,
                              SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 1.5,
                              Skyblue);
                              
}

// Draws a filled circle with constant diameter at given coordinates
void putCircleAt(coord_t * points) {
    
    // Circle diameter
    const short diameter = 80 / 2;

    tumDrawCircle(points->x, points->y, diameter, Red);

}


typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

void xGetButtonInput(void) {
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}


#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR
#define buttons_DEBOUNCE_DELAY	(200 / portTICK_RATE_MS)

/* Prevents buttons from bouncing:
   returns 1 if debouncing has been achieved,
   and 0 if it can't be achieved */
uint8_t DebouncingCondition (void) {

    const int buttons_change_period = buttons_DEBOUNCE_DELAY;

    if (xTaskGetTickCount() - buttons_last_change > buttons_change_period) {
        buttons_last_change = xTaskGetTickCount();
        return 1;
    } else {
        return 0;
    }
}

/* Reacts and shows how many times S have been pushed
 * for task  3.2.3 */
void vDrawButtonTextSwitching(void) {

	static char str[100] = { 0 };
	
	// Prints the S button counter
	sprintf(str, "S: %d", buttonCounter.counter);
	tumDrawText(str, 10, SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 2, Black);

	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
	
		// Toggles counting tasks when S is pressed (task 3.2.3)
		if (buttons.buttons[KEYCODE(S)] && DebouncingCondition()) {
			
			toggle_tasks++;
			
			if (toggle_tasks == 2)
				toggle_tasks = 0;
			
			switch(toggle_tasks) {
				// Resume the first task with a semaphore
				case 0:
					xSemaphoreGive(semSwitchingTask1);
					break;
					
				// Resume the second task with a 
				case 1:
					xTaskNotify(SwitchingTask2, 0, eNoAction);
					break;
				
				default:
					break;
			}
		}
			
		xSemaphoreGive(buttons.lock);
	
	}
}

// Blinks with 1 Hz
void vCircleBlink1(void *pvParameters) {

    coord_t circle_coord = {
        SCREEN_WIDTH/2 + SCREEN_WIDTH/6,
        SCREEN_HEIGHT/2
    };

  // tumDrawBindThread(); // Drawing control

    while (1) {

        // Draw a circle in the middle of the screen
        putCircleAt(&circle_coord);
  //      tumDrawUpdateScreen();
        vTaskDelay(500 / portTICK_RATE_MS);
        tumDrawClear(White);
   //     tumDrawUpdateScreen();

        vTaskDelay(500 / portTICK_RATE_MS);

  //              tumDrawUpdateScreen();
    }
}

// Blinks with 2 Hz
void vCircleBlink2(void *pvParameters) {

    coord_t circle_coord = {
        SCREEN_WIDTH/2 -  SCREEN_WIDTH/6,
        SCREEN_HEIGHT/2
    };

 //   tumDrawBindThread(); // Drawing control

    while (1) {
        // Draw a circle in the middle of the screen
        putCircleAt(&circle_coord);
        vTaskDelay(250 / portTICK_RATE_MS);
        tumDrawClear(White);
        vTaskDelay(250 / portTICK_RATE_MS);
    }
}

void vSwapBuffers(void *pvParameters) {

    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t frameratePeriod = 20;

    tumDrawBindThread(); // Setup Rendering handle with correct GL context

    while (1) {
            tumEventFetchEvents(FETCH_EVENT_NONBLOCK); // Query events backend for new events, ie. button presses
            xGetButtonInput(); // Update global input
            vDrawButtonTextSwitching();
            vDrawFPS();

            tumDrawUpdateScreen();
            vTaskDelayUntil(&xLastWakeTime, frameratePeriod);
    }
}

// The task that requires a binary semaphore
void vSwitchingTask1 (void *pvParameters) {

	while(1) {
	
		// Blocks to wait for a semaphore
		if (xSemaphoreTake(semSwitchingTask1, portMAX_DELAY) == pdPASS) {

			if (xSemaphoreTake(buttonCounter.lock, 0) == pdPASS) {
				buttonCounter.counter++;
				xSemaphoreGive(buttonCounter.lock);
			}
		}
	}
}

// The task that requires a notification
void vSwitchingTask2 (void *pvParameters) {

	while(1) {
	
		// Blocks to wait for a notification
		ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

		if (xSemaphoreTake(buttonCounter.lock, 0) == pdPASS) {
			buttonCounter.counter++;
			xSemaphoreGive(buttonCounter.lock);
		}
	}
}
 
// The task that resets the button's counter
void vSwitchingTimer (void *pvParameters) {

    TickType_t xLastWakeTimeTimer;

    xLastWakeTimeTimer = xTaskGetTickCount();

	while(1) {
		if (xSemaphoreTake(buttonCounter.lock, 0) == pdPASS) {
			buttonCounter.counter = 0;	
			xSemaphoreGive(buttonCounter.lock);
		}

        vTaskDelayUntil(&xLastWakeTimeTimer, 10000 / portTICK_RATE_MS);

	}
    

}

int main(int argc, char *argv[]) {

    char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);


    tumDrawInit(bin_folder_path); // Inialize drawing
    tumEventInit(); // Initalize events

    semSwitchingTask1 = xSemaphoreCreateBinary(); // Locking mechanism for the first task in the exercise 3.2.3
    buttonCounter.lock = xSemaphoreCreateMutex(); // Mutex to protect buttonCounter.counter
    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism for buttons

    xTaskCreate(vSwapBuffers, "BufferSwapTask", mainGENERIC_STACK_SIZE, NULL, configMAX_PRIORITIES, BufferSwap);
    xTaskCreate(vCircleBlink1, "Blinks1Hz", mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY + 1, NULL);
    xTaskCreateStatic(vCircleBlink2, "Blinks2Hz", mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY , xStack, &xTaskBuffer);
    xTaskCreate(vSwitchingTask1, "SwitchingTask1", mainGENERIC_STACK_SIZE, NULL, tskIDLE_PRIORITY, &SwitchingTask1);
    xTaskCreate(vSwitchingTask2, "SwitchingTask2", mainGENERIC_STACK_SIZE, NULL, tskIDLE_PRIORITY, &SwitchingTask2);
    xTaskCreate(vSwitchingTimer, "SwitchingTimer", mainGENERIC_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);

    vTaskStartScheduler();

    return EXIT_SUCCESS;

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
