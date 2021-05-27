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
#include "timers.h" // Add software timers

#include "TUM_Draw.h"
#include "TUM_Utils.h"
#include "TUM_Event.h"
#include "TUM_Font.h"

#define mainGENERIC_STACK_SIZE ((unsigned short)1000)
#define FRAME_RATE_FREQUENCY 20

#include "static_allocation.h" // contains implementation of functions required for static memory allocation  

// General parameters
#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define MAX_TICKS 15 // Maximum number of ticks for the four ticking tasks (exercise 4.0.2)

// State machine parameters
#define EXERCISE_THREE 0
#define EXERCISE_FOUR 1
#define STARTING_STATE EXERCISE_THREE
#define STATE_COUNT 2
#define STATE_DEBOUNCE_DELAY 300

static TaskHandle_t BufferSwap = NULL;
static TaskHandle_t SwitchingTask2 = NULL; // Synchronization of the second keystroke counter (exercise 3.2.3)
static TaskHandle_t IncrementVariable = NULL; // Handles for the tasks for incrementing a variable (exercise 3.2.4)
static TaskHandle_t CircleBlink1 = NULL;
static TaskHandle_t SwitchingTask1 = NULL;
static TaskHandle_t printTextExercise3 = NULL;
static TaskHandle_t printTextExercise4 = NULL;
static TaskHandle_t CircleBlink2_dynamic = NULL;


static TimerHandle_t xTimer = NULL; // A timer's handle (exercise 3.2.3)

static QueueHandle_t ticksMsgQueue = NULL; // Queue to keep messages for the four ticking tasks (exercise 4.0.2)

static SemaphoreHandle_t ScreenLock = NULL;
static SemaphoreHandle_t semStateMachine = NULL; // Semaphore for going to the text state in the state machine

// Type for messages between the ticking tasks (exercise 4.0.2)
typedef struct {
	TickType_t tickNo;
	uint8_t message;
} message_t;

// Data structure for sychronized counting of button's pushes (exercise 3.2.3)
typedef struct switchingButtonCounter {
    unsigned int counter;
    SemaphoreHandle_t lock;
} switchingButtonCounter_t;
switchingButtonCounter_t buttonCounter = { 0 };

// Stores last time when a button was pressed
TickType_t buttons_last_change;

// A variable to be incremented (task 3.2.4)
int variableToIncrement = 0;

// Semaphore to resume vSwitchingTask1 (exercise 3.2.3)
SemaphoreHandle_t semSwitchingTask1 = NULL;

/* ------------------------- Functions ------------------------- */

// Prints the description how to use the program in exercise 3
void vDrawHelpTextExercise3(void) {
    tumDrawText("Exercise 3", SCREEN_WIDTH - 640/2 - 50, DEFAULT_FONT_SIZE * 0 , Red);
}

// Prints the description how to use the program in exercise 3
void vDrawHelpTextExercise4(void) {
    tumDrawText("Exercise 4", SCREEN_WIDTH - 640/2 - 50, DEFAULT_FONT_SIZE * 0 , Red);
}

#define FPS_AVERAGE_COUNT 50

// Draws FPS
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

    tumDrawText(str, SCREEN_WIDTH/2 - 20,
                              SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 8,
                              Black);
                              
}

// Draws a filled circle at given coordinates
void putCircleAt(coord_t points) {
    
    // Circle diameter
    const short diameter = 80 / 2;

    tumDrawCircle(points.x, points.y, diameter, Red);

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
    // For debugging
    else {
        uint8_t xGetButtonInputDidntGotSemaphore = 1;
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

/* Returns a char if number is not zero,
   otherwise returns an empty char */
char xTidyPrint(uint8_t number) {

    if (number != 0) {
        return number + '0';
    } else {
        return ' ';
    }
}

/* Prints the output from the four ticking functions:
   requires a 2D array to fulfill, last tick number and item number */ 
void vPrintMsgArray(uint8_t arrayToPrint[][4], uint8_t itemsCounter[]) {

	static char str[100] = { 0 };
	message_t msgTaskTicks;
	
    // Get messages from the queue unless it's empty
	if (uxQueueMessagesWaiting(ticksMsgQueue)) {

        tumDrawClear(White);

        xQueueReceive(ticksMsgQueue, &msgTaskTicks, portMAX_DELAY);

        arrayToPrint[msgTaskTicks.tickNo - 1] [ itemsCounter[msgTaskTicks.tickNo - 1] ] = msgTaskTicks.message;
        itemsCounter[msgTaskTicks.tickNo - 1]++;
  
    }

    // Print the array of messages
    for (uint8_t tick = 0; tick < msgTaskTicks.tickNo; tick++) {

        sprintf(str, "TickNo: %d, Output: %c%c%c%c", tick + 1,
            xTidyPrint(arrayToPrint[tick][0]),
            xTidyPrint(arrayToPrint[tick][1]),
            xTidyPrint(arrayToPrint[tick][2]),
            xTidyPrint(arrayToPrint[tick][3]));

	    tumDrawText(str, 10, DEFAULT_FONT_SIZE * tick, Black);
    } 
}

void vDrawButtonText(void) {

	static char str[100] = { 0 };
	
    // Clear the button's counter text
   // tumDrawFilledBox(0, SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 2, 250, DEFAULT_FONT_SIZE * 3, White);

	// Prints the buttons' counter
	sprintf(str, "Keystokes: %d", buttonCounter.counter);
	tumDrawText(str, 10, SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 5, Black);
    tumDrawText("Push S to call a semaphore task,", 10, SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 4, Black);
    tumDrawText("push N to call a notification task,", 10, SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 3, Black);
    tumDrawText("the timer will reset the value every 15 sec", 10, SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 2, Black);
}

// Reacts to keyaboard input
void vButtonSwitching(void) {

	if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
	
		// Triggers the task with a semaphore (exercise 3.2.3)
		if (buttons.buttons[KEYCODE(S)] && DebouncingCondition())
			xSemaphoreGive(semSwitchingTask1);

        // Triggers the task with a notification (exercise 3.2.3)
        if (buttons.buttons[KEYCODE(N)] && DebouncingCondition())
			xTaskNotify(SwitchingTask2, 0, eNoAction);

        // Trigger the vIncrementVariable (exercise 3.2.4)
        if (buttons.buttons[KEYCODE(P)] && DebouncingCondition()) {

        	if (eTaskGetState(IncrementVariable) == eSuspended)
		        vTaskResume(IncrementVariable);
	        else
		        vTaskSuspend(IncrementVariable);
        }
		
  /*      if (buttons.buttons[KEYCODE(E)] && DebouncingCondition()) {

            xQueueSend(StateQueue, &next_state_signal, 0);

        }
  */

		xSemaphoreGive(buttons.lock);
	
	}
}

// Resets the button's counter when the timer fires (exercise 3.2.3)
 void vTimerResetCounter(TimerHandle_t xTimer) {
 
 	if (xSemaphoreTake(buttonCounter.lock, 0) == pdPASS) {
			buttonCounter.counter = 0;	
			xSemaphoreGive(buttonCounter.lock);
	}
}

// Prints the incremented variable (exercise 3.2.4)
void vDrawTextIncrementedVar(void) {

	static char str[100] = { 0 };

	// Clear the button's counter text
//    tumDrawFilledBox(0, SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 2, 250, DEFAULT_FONT_SIZE *2,White);

	// Print the counter
	sprintf(str, "Variable incremented every second: %d", variableToIncrement);
	tumDrawText(str, SCREEN_WIDTH - 300, SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 3, Black);
    tumDrawText("Push P to pause", SCREEN_WIDTH - 300, SCREEN_HEIGHT - DEFAULT_FONT_SIZE * 2, Black);

}


// Checks if the state changing button E was pressed
void vCheckStateInput(void) {

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {

    	if (buttons.buttons[KEYCODE(E)] && DebouncingCondition()) {

            xSemaphoreGive(semStateMachine); // Give a semaphore to the state machine

            xSemaphoreGive(buttons.lock);

        }
    }
}

/* ------------------------------- Tasks ------------------------------- */

// Blinks with 1 Hz
void vCircleBlink1(void *pvParameters) {

    coord_t circle_coord = {
        SCREEN_WIDTH/2 + SCREEN_WIDTH/7,
        SCREEN_HEIGHT/2
    };

    while (1) {

        tumDrawCircle(circle_coord.x, circle_coord.y, 40, Blue);
        vTaskDelay(500 / portTICK_RATE_MS);
        tumDrawFilledBox(0, 0, 640, 350, White); // Clean the area
        vTaskDelay(500 / portTICK_RATE_MS);
    }
}

// Blinks with 2 Hz
void vCircleBlink2(void *pvParameters) {

    coord_t circle_coord = {
        SCREEN_WIDTH/2 -  SCREEN_WIDTH/7,
        SCREEN_HEIGHT/2
    };

    while (1) {
        
        tumDrawCircle(circle_coord.x, circle_coord.y, 40, Red);
        vTaskDelay(250 / portTICK_RATE_MS);
        tumDrawFilledBox(0, 0, 640, 350, White); // Clean the area
        vTaskDelay(250 / portTICK_RATE_MS);
    }
}

// The task that refreshes the screen
void vSwapBuffers(void *pvParameters) {

    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t framerateFrequency = FRAME_RATE_FREQUENCY;

    tumDrawBindThread(); // Setup Rendering handle with correct GL context

    while (1) {

        if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {

            tumDrawUpdateScreen();
            xSemaphoreGive(ScreenLock);

        }

            vTaskDelayUntil(&xLastWakeTime, framerateFrequency);
    }
}

// Prints and processes input for exercise 3
void vPrintTextExercise3 (void *pvParameters) {

    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t framerateFrequency = FRAME_RATE_FREQUENCY;

    while (1) {
            
            if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {

            tumDrawFilledBox(0, 350, 640, 130, White); // Clean the text area

            vDrawFPS();
            vDrawButtonText(); // Draw keystrokes counter
            vDrawTextIncrementedVar(); //Draw a variable that increments every second
            tumEventFetchEvents(FETCH_EVENT_NONBLOCK |
                 FETCH_EVENT_NO_GL_CHECK); // Query events backend for new events, ie. button presses
            xGetButtonInput(); // Update global buttons input
            vButtonSwitching(); // Process keyboard input for keystrokes counters and the timer
            
            vCheckStateInput(); // Check if the state needs to be changed (E button)

            xSemaphoreGive(ScreenLock);
            }

            vTaskDelayUntil(&xLastWakeTime, framerateFrequency);
    }
}

// Prints and processes input for exercise 4
void vPrintTextExercise4 (void *pvParameters) {

    uint8_t msgArray[MAX_TICKS][4] = { 0 }; // Array to hold messages from the four ticking tasks
    uint8_t itemsCount[15] = { 0 }; // Number of items for each tick

    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t framerateFrequency = FRAME_RATE_FREQUENCY;

    while (1) {
        
  //      if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {

        vPrintMsgArray(msgArray, itemsCount);
   //     xSemaphoreGive(ScreenLock);

   //     }

        vTaskDelayUntil(&xLastWakeTime, framerateFrequency);
    }
}

// The task that requires a binary semaphore (exercise 3.2.3)
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

// The task that requires a notification  (exercise 3.2.3)
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

// The task that increases a variable every second (exercise 3.2.4)
void vIncrementVariable (void *pvParameters) {

	while(1) {
        vTaskDelay(1000 / portTICK_RATE_MS);
		variableToIncrement++;
	}	
}


// Task 1 for counting every first tick (exercise 4.0.2)
void vTaskTicks1 (void *pvParameters) {

	TickType_t xLastWakeTime, firstRun;
	xLastWakeTime = firstRun = xTaskGetTickCount();
	
	message_t msgTaskTicks;
	
	while(1) {
		vTaskDelayUntil(&xLastWakeTime, 1);

        // Suspend the task after 15 ticks
        if (xLastWakeTime - firstRun > (TickType_t) MAX_TICKS)
		    vTaskSuspend(NULL);
		
		// Write tick number
		msgTaskTicks.tickNo =  xLastWakeTime - firstRun;
		// Write a message
		msgTaskTicks.message = 1;
		
		// Send a message
		xQueueSend(ticksMsgQueue, &msgTaskTicks, portMAX_DELAY);
	}	
}

// Task 2 for counting every second tick  (exercise 4.0.2)
void vTaskTicks2 (void *pvParameters) {

	TickType_t xLastWakeTime, firstRun;
	xLastWakeTime = firstRun = xTaskGetTickCount();
	
	message_t msgTaskTicks;
	
	while(1) {
		vTaskDelayUntil(&xLastWakeTime, 2);
        
        // Suspend the task after 15 ticks
        if (xLastWakeTime - firstRun > (TickType_t) MAX_TICKS)
		    vTaskSuspend(NULL);
		
		// Write tick number
		msgTaskTicks.tickNo =  xLastWakeTime - firstRun;
		// Write a message
		msgTaskTicks.message = 2;
		
		// Send a message
		xQueueSend(ticksMsgQueue, &msgTaskTicks, portMAX_DELAY);
	}	
}

// Task 3 for counting every third tick (exercise 4.0.2)
void vTaskTicks3 (void *pvParameters) {

	TickType_t xLastWakeTime, firstRun;
	xLastWakeTime = firstRun = xTaskGetTickCount();
	
	message_t msgTaskTicks;
	
	while(1) {
		vTaskDelayUntil(&xLastWakeTime, 3);
        
        // Suspend the task after 15 ticks
        if (xLastWakeTime - firstRun > (TickType_t) MAX_TICKS)
		    vTaskSuspend(NULL);
		
		// Write tick number
		msgTaskTicks.tickNo =  xLastWakeTime - firstRun;
		// Write a message
		msgTaskTicks.message = 3;
		
		// Send a message
		xQueueSend(ticksMsgQueue, &msgTaskTicks, portMAX_DELAY);
	}	
}

// Task 4 for counting every fourth tick (exercise 4.0.2)
void vTaskTicks4(void *pvParameters) {

	TickType_t xLastWakeTime, firstRun;
	xLastWakeTime = firstRun = xTaskGetTickCount();
	
	message_t msgTaskTicks;
	
	while(1) {
		vTaskDelayUntil(&xLastWakeTime, 4);
        
        // Suspend the task after 15 ticks
        if (xLastWakeTime - firstRun > (TickType_t) MAX_TICKS)
		    vTaskSuspend(NULL);
		
		// Write tick number
		msgTaskTicks.tickNo =  xLastWakeTime - firstRun;
		// Write a message
		msgTaskTicks.message = 4;
		
		// Send a message
		xQueueSend(ticksMsgQueue, &msgTaskTicks, portMAX_DELAY);
	}	
}

// State machine task
void vStateMachine(void *pvParameters) {

    unsigned char current_state = STARTING_STATE; // Default state
    unsigned char state_changed = 1; // Only re-evaluate state if it has changed

    while (1) {
        if (state_changed) {
            goto initial_state;
        }

		if (xSemaphoreTake(semStateMachine, 0) == pdPASS) {

			current_state++;

            if (current_state > STATE_COUNT)
                current_state = EXERCISE_THREE;

            state_changed = 1;
        }
 
initial_state:
        // Handle current state
        if (state_changed) {

            switch (current_state) {                    
                case EXERCISE_THREE:
                    vTaskResume(printTextExercise4);
                    vTaskResume(CircleBlink1);
                	vTaskResume(CircleBlink2_dynamic);
                	vTaskResume(SwitchingTask1);
                	vTaskResume(SwitchingTask2);
                    vTaskResume(IncrementVariable);
                    vTaskResume(printTextExercise3);
                    xTimerStart(xTimer, 0);
                    break;
                        
                case EXERCISE_FOUR:
                	vTaskSuspend(CircleBlink1);
                	vTaskSuspend(CircleBlink2_dynamic);
                    vTaskSuspend(printTextExercise3);
                	vTaskSuspend(SwitchingTask1);
                	vTaskSuspend(SwitchingTask2);
                	vTaskSuspend(IncrementVariable);
                    vTaskResume(printTextExercise4);
                	xTimerStop(xTimer, 0);
                	break;
                
                default:
                    break;
            }

            state_changed = 0;
        }

        vTaskDelay(STATE_DEBOUNCE_DELAY);
    }
}

int main(int argc, char *argv[]) {

    char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);

    tumDrawInit(bin_folder_path); // Inialize drawing
    tumDrawClear(White);
    tumEventInit(); // Initalize events

    semSwitchingTask1 = xSemaphoreCreateBinary(); // Locking mechanism for the first task (exercise 3.2.3)
    semStateMachine = xSemaphoreCreateBinary(); // Semaphore for going to the text state in the state machine
    buttonCounter.lock = xSemaphoreCreateMutex(); // Mutex to protect buttonCounter.counter (exercise 3.2.3)
    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism for buttons
    ScreenLock = xSemaphoreCreateMutex(); // Locking mechanism for the screen
	
	xTimer = xTimerCreate("ResetTimer", pdMS_TO_TICKS(15000), pdTRUE, ( void * ) 0, vTimerResetCounter); // Set the timer to 15 sec

    ticksMsgQueue = xQueueCreate(8*15, sizeof(message_t)); // Queue for the messages from the ticking functions (exercise 4.0.2)

    xTaskCreate(vSwapBuffers, "BufferSwapTask", mainGENERIC_STACK_SIZE, NULL, configMAX_PRIORITIES, BufferSwap); // Screen update

    // Blinking circles
    xTaskCreate(vCircleBlink1, "Blinks1Hz", mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY + 1, &CircleBlink1);
    xTaskCreate(vCircleBlink2, "Blinks2Hz", mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY, &CircleBlink2_dynamic);

    // Static task creatinon (doesn't work with xTaskResume for some reason)
    // xTaskCreateStatic(vCircleBlink2, "Blinks2Hz", mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY , xStack, &CircleBlink2);

    // Keystrokes counters
    xTaskCreate(vSwitchingTask1, "SwitchingTask1", mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY, &SwitchingTask1); // activated by a semaphore
    xTaskCreate(vSwitchingTask2, "SwitchingTask2", mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY +1, &SwitchingTask2); // activated by notification
 
    xTaskCreate(vPrintTextExercise3, "PrintTextExercise3", mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY+2, &printTextExercise3); // Output text for exercise 3
    xTaskCreate(vPrintTextExercise4, "PrintTextExercise4", mainGENERIC_STACK_SIZE, NULL, mainGENERIC_PRIORITY+2, &printTextExercise4); // Output text for exercise 4

    xTimerStart(xTimer, 0); // Start the timer

    // Increments a varible every second
    xTaskCreate(vIncrementVariable, "IncrementVariable", mainGENERIC_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, &IncrementVariable);

    // The ticking tasks (exercise 4.0.2)
    xTaskCreate(vTaskTicks1, "TaskTicks1", mainGENERIC_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(vTaskTicks2, "TaskTicks2", mainGENERIC_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(vTaskTicks3, "TaskTicks3", mainGENERIC_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(vTaskTicks4, "TaskTicks4", mainGENERIC_STACK_SIZE, NULL, 4, NULL);

    // State machine
    xTaskCreate(vStateMachine, "StateMachine", mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES - 2, NULL);

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
