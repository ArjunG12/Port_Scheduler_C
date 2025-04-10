#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <sys/msg.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#define MAX_DOCKS 30
#define MAX_AUTH_STRING_LEN 100
#define MAX_NEW_REQUESTS 100
#define MAX_CARGO_COUNT 200
static int msgID;
typedef struct crane {
	int capacity;
	int occupied;
	int craneId;
} crane;

typedef struct docInfo {
	crane crane_[30];
	int occupied;
	int category;
	int docId;
} docInfo;

typedef struct ShipRequest {
	int shipId;
	int timestep;
	int category;
	int direction;
	int emergency;
	int waitingTime;
	int numCargo;
	int cargo[MAX_CARGO_COUNT];
} ShipRequest;

typedef struct Shp {
	ShipRequest req;
	int docked;
	int doc_time;
	int cargoId[MAX_CARGO_COUNT];
} Shp;

typedef struct inputTextData {
	int sharedMemoryKey;
	int MainMessageQueueKey;
	int m;
	int solverSchedulerKeys[8];
	int n;
	docInfo doc_info[25];
	int maxCategory;
} inputTextData;

typedef struct MessageStruct {
	long mtype;
	int timestep;
	int shipId;
	int direction;
	int dockId;
	int cargoId;
	int isFinished;
	union {
		int numShipRequests;
		int craneId;
	};
} MessageStruct;

typedef struct MainSharedMemory {
	char authStrings[MAX_DOCKS][MAX_AUTH_STRING_LEN];
	ShipRequest newShipRequests[MAX_NEW_REQUESTS];
} MainSharedMemory;
typedef struct SolverRequest {
    long mtype;
    int dockId;
    char authStringGuess[MAX_AUTH_STRING_LEN];
} SolverRequest;

typedef struct SolverResponse {
    long mtype;
    int guessIsCorrect;
} SolverResponse;

// Function definitions...

int totalCraneCapacity(const docInfo *dock) {
	int sum = 0;
	for (int i = 0; i < dock->category; i++) {
		sum += dock->crane_[i].capacity;
	}
	return sum;
}

int compareDocInfo(const void *a, const void *b) {
	const docInfo *dockA = (const docInfo *)a;
	const docInfo *dockB = (const docInfo *)b;

	if (dockA->category != dockB->category) {
		return dockB->category - dockA->category;
	}
	return totalCraneCapacity(dockB) - totalCraneCapacity(dockA);
}

int compareCranes(const void *a, const void *b) {
	const crane *craneA = (const crane *)a;
	const crane *craneB = (const crane *)b;
	return craneB->capacity - craneA->capacity;
}

int compareShipRequests(const void *a, const void *b) {
	const ShipRequest *reqA = (const ShipRequest *)a;
	const ShipRequest *reqB = (const ShipRequest *)b;

	// Emergency ships get highest priority
	if (reqA->emergency != reqB->emergency) {
		return reqB->emergency - reqA->emergency;
	}

	// Next, prioritize by category (higher first)
	if (reqA->category != reqB->category) {
		return reqB->category - reqA->category;
	}

	// Then by max cargo weight (higher first)
	int maxWeightA = 0, maxWeightB = 0;
	for (int i = 0; i < reqA->numCargo; i++) 
		if (reqA->cargo[i] > maxWeightA) 
			maxWeightA = reqA->cargo[i];

	for (int i = 0; i < reqB->numCargo; i++) 
		if (reqB->cargo[i] > maxWeightB) 
			maxWeightB = reqB->cargo[i];

	if (maxWeightA != maxWeightB) {
		return maxWeightB - maxWeightA;
	}

	// Finally, break ties by number of cargo items (more first)
	return reqB->numCargo - reqA->numCargo;
}


int compareRemainingRequests(const void *a, const void *b) {
	const Shp *shipA = (const Shp *)a;
	const Shp *shipB = (const Shp *)b;
	
	 ShipRequest *reqA = &(shipA->req);
	 ShipRequest *reqB = &(shipB->req);
	// Emergency ships get highest priority
	if (reqA->emergency != reqB->emergency) {
		return reqA->emergency - reqB->emergency;
	}

	// Next, prioritize by category (higher first)
	if (reqA->category != reqB->category) {
		return reqB->category - reqA->category;
	}

	// Then by max cargo weight (higher first)
	int maxWeightA = 0, maxWeightB = 0;
	for (int i = 0; i < reqA->numCargo; i++) 
		if (reqA->cargo[i] > maxWeightA) 
			maxWeightA = reqA->cargo[i];

	for (int i = 0; i < reqB->numCargo; i++) 
		if (reqB->cargo[i] > maxWeightB) 
			maxWeightB = reqB->cargo[i];

	if (maxWeightA != maxWeightB) {
		return maxWeightB - maxWeightA;
	}

	// Finally, break ties by number of cargo items (more first)
	return reqB->numCargo - reqA->numCargo;
}

inputTextData *readInputFile(int x) {
	char filename[50];
	sprintf(filename, "./testcase%d/input.txt", x);

	FILE *file = fopen(filename, "r");
	if (!file) {
		perror("Error opening input file");
		exit(EXIT_FAILURE);
	}

	inputTextData *data = malloc(sizeof(inputTextData));
	if (!data) {
		perror("Memory allocation failed");
		exit(EXIT_FAILURE);
	}

	fscanf(file, "%d", &data->sharedMemoryKey);
	fscanf(file, "%d", &data->MainMessageQueueKey);
	fscanf(file, "%d", &data->m);

	for (int i = 0; i < data->m; i++)
		fscanf(file, "%d", &data->solverSchedulerKeys[i]);

	fscanf(file, "%d", &data->n);

	int max_cat = 0;
	for (int i = 0; i < data->n; i++) {
		int dockCategory;
		fscanf(file, "%d", &dockCategory);
		data->doc_info[i].category = dockCategory;
		data->doc_info[i].docId = i;
		data->doc_info[i].occupied = 0;

		if (dockCategory > max_cat) max_cat = dockCategory;

		for (int j = 0; j < dockCategory; j++) {
			data->doc_info[i].crane_[j].occupied = 0;
			fscanf(file, "%d", &data->doc_info[i].crane_[j].capacity);
			data->doc_info[i].crane_[j].craneId = j;
		}
	}
	data->maxCategory = max_cat;
	fclose(file);
	return data;
}

void printMessageDetails(MessageStruct message) {
	printf("Received queue message:\n");
	printf("Timestep: %d\n", message.timestep);
	printf("Ship ID: %d\n", message.shipId);
	printf("Direction: %d\n", message.direction);
	printf("Dock ID: %d\n", message.dockId);
	printf("Cargo ID: %d\n", message.cargoId);
	printf("Is Finished: %d\n", message.isFinished);
	printf("Union Field (numShipRequests/craneId): %d\n", message.numShipRequests);
}

void printSharedMem(ShipRequest req, int i) {
	printf("\n=== Ship Request %d ===", i + 1);
	printf("\nShip ID: %d", req.shipId);
	printf("\nArrival Timestep: %d", req.timestep);
	printf("\nCategory: %d", req.category);
	printf("\nDirection: %s", (req.direction == 1) ? "Incoming" : "Outgoing");
	printf("\nEmergency: %s", req.emergency ? "Yes" : "No");
	if (req.direction == 1 && !req.emergency) {
		printf("\nWaiting Time: %d timesteps", req.waitingTime);
	}
	printf("\nCargo Details:");
	printf("\n  Number of items: %d", req.numCargo);
	printf("\n  Weights: ");
	for (int j = 0; j < req.numCargo; j++) {
		printf("%d ", req.cargo[j]);
	}
	printf("\nCategory Constraint: Requires dock >= category %d", req.category);
	printf("\n=================================\n");
}

void printShipSituation(const Shp *ship) {
	printf("\n=== Ship Situation ===\n");
	printf("Ship ID: %d\n", ship->req.shipId);
	printf("Arrival Timestep: %d\n", ship->req.timestep);
	printf("Category: %d\n", ship->req.category);
	printf("Direction: %s\n", (ship->req.direction == 1) ? "Incoming" : "Outgoing");
	printf("Emergency: %s\n", (ship->req.emergency == 1) ? "Yes" : "No");

	if (ship->req.direction == 1 && !ship->req.emergency) {
		printf("Waiting Time: %d timesteps\n", ship->req.waitingTime);
	}

	printf("Cargo Details:\n");
	printf("  Number of items: %d\n", ship->req.numCargo);
	printf("  Weights: ");
	for (int i = 0; i < ship->req.numCargo; i++) {
		printf("%d ", ship->req.cargo[i]);
	}
	printf("\n");

	if (ship->docked == -1) {
		printf("Status: Not docked yet.\n");
	} else {
		printf("Status: Docked at dock number(not ID) %d.\n", ship->docked);
	}

	printf("======================\n");
}
void sendCargoMessage(int cap,int msgid, int timestep, int dockId, int shipId, int direction, int cargoWeight, int craneId) {
	MessageStruct msg = {
		.mtype = 4,
		.timestep = timestep,
		.dockId = dockId,
		.shipId = shipId,
		.direction = direction,
		.cargoId = cargoWeight,
		.craneId = craneId
	};
	
	// Print message before sending
	printf("\nSending cargo message:\n");
	printf("Timestep: %d\n", msg.timestep);
	printf("Dock ID: %d\n", msg.dockId);
	printf("Ship ID: %d\n", msg.shipId);
	printf("Direction: %s\n", (msg.direction == 1) ? "Incoming" : "Outgoing");
	printf("Cargo Weight: %d\n", msg.cargoId);
	printf("Crane ID: %d\n", msg.craneId);
	printf("Crane Capacity: %d\n", cap);
	if (msgsnd(msgid, &msg, sizeof(MessageStruct) - sizeof(long), 0) == -1) {
		
		perror("msgsnd failed");
		exit(EXIT_FAILURE);
	}
}

void shipDocMessage(int msgid, int timestep, int dockId, int shipId, int direction) {
	MessageStruct msg = {
		.mtype = 2,
		.timestep = timestep,
		.dockId = dockId,
		.shipId = shipId,
		.direction = direction
	};

	if (msgsnd(msgid, &msg, sizeof(MessageStruct) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		exit(EXIT_FAILURE);
	}
}
void updateTimestep(int msgid, int timestep){
	MessageStruct msg = {
		.mtype = 5,
		.timestep = timestep
	};
	if (msgsnd(msgid, &msg, sizeof(MessageStruct) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		exit(EXIT_FAILURE);
	}
}
void initializeIPC(inputTextData *inputFile, int *msgid, MainSharedMemory **shared_mem) {
	key_t key = (key_t)inputFile->MainMessageQueueKey;
	*msgid = msgget(key, 0666);
	if (*msgid == -1) {
		perror("msgget failed");
		exit(EXIT_FAILURE);
	}

	key_t shm_key = (key_t)inputFile->sharedMemoryKey;
	int shm_id = shmget(shm_key, sizeof(MainSharedMemory), 0666);
	if (shm_id == -1) {
		perror("shmget failed");
		exit(EXIT_FAILURE);
	}

	*shared_mem = shmat(shm_id, NULL, 0);
	if (*shared_mem == (void *)-1) {
		perror("shmat failed");
		exit(EXIT_FAILURE);
	}
}

void sortDocksAndCranes(inputTextData *inputFile) {
	qsort(inputFile->doc_info, inputFile->n, sizeof(docInfo), compareDocInfo);
	for (int i = 0; i < inputFile->n; i++) {
		qsort(inputFile->doc_info[i].crane_, inputFile->doc_info[i].category, sizeof(crane), compareCranes);
	}
}

void copyRemainingRequest(ShipRequest *dst, ShipRequest *src) {
	*dst = *src;
	int count = 0;
	for (int i = 0; i < src->numCargo; i++) {
		if (src->cargo[i] != -1)
			dst->cargo[count++] = src->cargo[i];
	}
	dst->numCargo = count;
	
}
int  shipUndocking(inputTextData *inputFile,MainSharedMemory *shared_mem,int dockId, int shipId, int direction, 
		int dockTime, int lastCargoMovedTime ,int* solver_in_use,int shipsUndoc) {
		
	key_t key = (key_t)inputFile->MainMessageQueueKey;
	int msgQueueId = msgget(key, 0666);
	if (msgQueueId == -1) {
		perror("Failed to get main message queue");
		exit(EXIT_FAILURE);
	}

	int numSolvers = inputFile->m;
	int solverMsgQueueIds[numSolvers];
	for (int i = 0; i < numSolvers; i++) {
		key_t solverKey = (key_t)inputFile->solverSchedulerKeys[i];
		solverMsgQueueIds[i] = msgget(solverKey, 0666);
		if (solverMsgQueueIds[i] == -1) {
			perror("Failed to get solver message queue");
			exit(EXIT_FAILURE);
		}
	}
	
	int authStringLength= lastCargoMovedTime - dockTime-1;
	if (authStringLength <= 0 || authStringLength >= MAX_AUTH_STRING_LEN) {
		fprintf(stderr, "Invalid auth string length for dock %d\n", dockId);
		return -1;
	}

	// Notify solvers about the dock being guessed for
	SolverRequest initReq = { .mtype = 1, .dockId = dockId };
	int solver_using=-1;
	for (int i = 0; i < numSolvers; i++) {
		if(solver_in_use[i]==-1){
			solver_in_use[i]=1;
			solver_using=i;
			if (msgsnd(solverMsgQueueIds[i], &initReq, sizeof(SolverRequest) - sizeof(long), 0) == -1) {
				perror("Failed to send initialization request to solver");
				exit(EXIT_FAILURE);	
			}
			break;
		}
			
	}
	printf("Using solver:%d",solver_using);
	
	if(solver_using==-1){
		printf("All solvers busy\n");
		return -1;
	}
	
	// Brute force guessing logic
	const char charset[] = {'5', '6', '7', '8', '9', '.'};
	int charsetLen = sizeof(charset) / sizeof(charset[0]);
	char guess[MAX_AUTH_STRING_LEN];
	guess[authStringLength] = '\0';

	SolverRequest guessReq = { .mtype = 2, .dockId = dockId };
	SolverResponse response;
	
	bool tryAuthString(int pos, int solverIdx) {
		for (int i = 0; i < charsetLen; i++) {
			// First and last characters cannot be '.'
			if ((pos == 0 || pos == authStringLength - 1) && charset[i] == '.') continue;
			
			guess[pos] = charset[i];
			if (pos == authStringLength - 1) {
				strncpy(guessReq.authStringGuess, guess, MAX_AUTH_STRING_LEN);

				// Send guess to solver
				//printf("Send guess to solver:%s\n",guess);
				if (msgsnd(solverMsgQueueIds[solverIdx], &guessReq, sizeof(SolverRequest) - sizeof(long), 0) == -1) {
					
					perror("Failed to send guess request to solver");
					exit(EXIT_FAILURE);
				}

				// Receive response from solver
				if (msgrcv(solverMsgQueueIds[solverIdx], &response, sizeof(SolverResponse) - sizeof(long), 3, 0) == -1) {
					perror("Failed to receive response from solver");
					exit(EXIT_FAILURE);
				}
				//printf("Response:%d\n",response.guessIsCorrect);
				// Check if guess is correct
				if (response.guessIsCorrect == 1) {
					
					return true;
				}
			} else {
				// Recursive call for next character
				if (tryAuthString(pos + 1, solverIdx)) return true;
			}
		}
		return false;
	}

	bool found = false;
	found = tryAuthString(0, solver_using);
	
	
	if (!found) {
		fprintf(stderr, "Failed to guess the correct auth string for dock %d\n", dockId);
		exit(EXIT_FAILURE);
	}
	
	strncpy(shared_mem->authStrings[dockId], guess, MAX_AUTH_STRING_LEN);
	printf("Sending string:%s to dockId:%d\n",guess,dockId);
	// Send undocking message to validation
	MessageStruct undockMsg = { 
		.mtype = 3,
		.dockId = dockId,
		.shipId = shipId,
		.direction = direction
	};

	if (msgsnd(msgQueueId, &undockMsg, sizeof(MessageStruct) - sizeof(long), 0) == -1) {
		perror("Failed to send undocking message");
		exit(EXIT_FAILURE);
	}

	printf("Undocking shipId:%d from docId:%d\n",shipId,dockId);

	// Allocate and return the correct auth string
	char *result = malloc(authStringLength + 1);
	if (!result) {
		perror("Failed to allocate memory for auth string");
		exit(EXIT_FAILURE);
	}
	strncpy(result, guess, authStringLength + 1);
	return 1;
}




int processRequests(int timestep,inputTextData *inputFile, MainSharedMemory *shared_mem, int msgid, MessageStruct message,int shipsRemained, Shp *remainingReq) {
	ShipRequest *requests = shared_mem->newShipRequests;
	int remainingShips = 0;
	qsort(requests, message.numShipRequests, sizeof(ShipRequest), compareShipRequests);
	
	int *solver_in_use=malloc(sizeof(int) * inputFile->m);
	for(int i=0;i<inputFile->m;i++)solver_in_use[i]=-1;
	
	
	//First Remaining requests
	int shipsUndocked=0;
	for(int y=0;y<shipsRemained;y++){
		
			printShipSituation(&remainingReq[y]);
		printf("The doc situation is:\n");
		for(int x=0;x<inputFile->n;x++){
			printf("Dock with ID:%d has occupied value of %d\n",inputFile->doc_info[x].docId,inputFile->doc_info[x].occupied);
		}
		if(remainingReq[y].docked==-1){
			ShipRequest req = remainingReq[y].req;
			int ShipDocked = 0;
			int doc_t = inputFile->n;
			for (; doc_t>=0; doc_t--) {
				if (inputFile->doc_info[doc_t].occupied != 0) continue;

				if (inputFile->doc_info[doc_t].category >= req.category) {
					inputFile->doc_info[doc_t].occupied = 1;
					shipDocMessage(msgid, timestep, inputFile->doc_info[doc_t].docId, req.shipId, req.direction);
					ShipDocked = 1;
					break;
				}
			}
			req.waitingTime--;
			if(req.waitingTime>=0 || (req.emergency==1)){
				copyRemainingRequest(&remainingReq[remainingShips].req, &req);
				if (ShipDocked){
					remainingReq[remainingShips].docked = doc_t;
					for(int j=0;j<req.numCargo;j++)remainingReq[remainingShips].cargoId[j]=j;
					remainingReq[remainingShips].doc_time=timestep;
					printf("Docked ship:%d at timestep:%d\n",req.shipId,remainingReq[remainingShips].doc_time);
					remainingShips++;
				}
				
				else {
					remainingReq[remainingShips++].docked = -1;
				}
			}
			
		}
		else if(remainingReq[y].req.numCargo==0){
			int doc_t=remainingReq[y].docked;
			ShipRequest req = remainingReq[y].req;
			
			printf("Current timestep:%d, dockTime:%d\n\n",timestep,remainingReq[y].doc_time);
			int doc_success=shipUndocking(inputFile,shared_mem,inputFile->doc_info[doc_t].docId,req.shipId,
						req.direction,remainingReq[y].doc_time,timestep,solver_in_use,shipsUndocked++);
			if(doc_success!=1){
				copyRemainingRequest(&remainingReq[remainingShips].req, &req);
				for(int weights_t=0, new_weights_t=0;weights_t<req.numCargo;weights_t++){
						if(remainingReq[y].cargoId[weights_t]!=-1){
							remainingReq[remainingShips].cargoId[new_weights_t]=remainingReq[y].cargoId[weights_t];
							new_weights_t++;
						}
				}
				remainingReq[remainingShips++].docked = doc_t;
			}
			inputFile->doc_info[doc_t].occupied =-2;
			
		}
		else{
			ShipRequest req = remainingReq[y].req;
			
			int doc_t=remainingReq[y].docked;
			int cargo_rem=0;
	
			for (int weights_t = 0; weights_t < req.numCargo; weights_t++) {
				for (int crane_t = inputFile->doc_info[doc_t].category - 1; crane_t >= 0; crane_t--) {
					if ((inputFile->doc_info[doc_t].crane_[crane_t].capacity >= req.cargo[weights_t]) &&
					    (inputFile->doc_info[doc_t].crane_[crane_t].occupied == 0) &&
					    (req.cargo[weights_t] != -1)) {

						inputFile->doc_info[doc_t].crane_[crane_t].occupied = 1;
						sendCargoMessage(inputFile->doc_info[doc_t].crane_[crane_t].capacity,
						msgid, timestep, inputFile->doc_info[doc_t].docId,
							req.shipId, req.direction, remainingReq[y].cargoId[weights_t],
							inputFile->doc_info[doc_t].crane_[crane_t].craneId);
						req.cargo[weights_t] = -1;
						remainingReq[y].cargoId[weights_t]=-1;
						cargo_rem++;
						break;
					}
				}
				
			}
			for (int crane_t = inputFile->doc_info[doc_t].category - 1; crane_t >= 0; crane_t--) {
					inputFile->doc_info[doc_t].crane_[crane_t].occupied = 0;
			}
			
			
			copyRemainingRequest(&remainingReq[remainingShips].req, &req);
			for(int weights_t=0, new_weights_t=0;weights_t<req.numCargo;weights_t++){
					if(remainingReq[y].cargoId[weights_t]!=-1){
						remainingReq[remainingShips].cargoId[new_weights_t]=remainingReq[y].cargoId[weights_t];
						new_weights_t++;
					}
			}
			remainingReq[remainingShips].doc_time=remainingReq[y].doc_time;
			remainingReq[remainingShips++].docked = doc_t;
		}
	}
	
	for (int i = 0; i < message.numShipRequests; i++) {
		ShipRequest req = requests[i];
		printSharedMem(req, i);
		
		int ShipDocked = 0;
		int doc_t=inputFile->n;
		for (; doc_t >=0 ; doc_t--) {
			if (inputFile->doc_info[doc_t].occupied != 0) continue;

			if (inputFile->doc_info[doc_t].category >= req.category) {
				inputFile->doc_info[doc_t].occupied = 1;
				shipDocMessage(msgid, req.timestep, inputFile->doc_info[doc_t].docId, req.shipId, req.direction);
				ShipDocked = 1;
				break;
			}
		}

		
		req.waitingTime--;
		copyRemainingRequest(&remainingReq[remainingShips].req, &req);
		
		if (ShipDocked){
			remainingReq[remainingShips].docked = doc_t;
			for(int j=0;j<req.numCargo;j++)remainingReq[remainingShips].cargoId[j]=j;
			remainingReq[remainingShips++].doc_time=timestep;
		}
		
		else {
			remainingReq[remainingShips++].docked = -1;
		}
	}
	
	for(int i=0;i<remainingShips;i++){
		printf("\nAfter timestep %d, remaining Ships are:\n",timestep);
		printShipSituation(&remainingReq[i]);
		printf("Docked at time:%d\n\n",remainingReq[i].doc_time);
		
	}
	
	for (int doc_t = 0; doc_t < inputFile->n; doc_t++) {
		if(inputFile->doc_info[doc_t].occupied ==-2)inputFile->doc_info[doc_t].occupied=0;
	}
	return remainingShips;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("Usage: %s <number>\n", argv[0]);
		return 1;
	}
	int n = atoi(argv[1]);
	printf("n = %d\n", n);
	inputTextData *inputFile = readInputFile(n);
	sortDocksAndCranes(inputFile);

	int msgid;
	MainSharedMemory *shared_mem;
	initializeIPC(inputFile, &msgid, &shared_mem);
	MessageStruct message;
	
	Shp *req = malloc(sizeof(Shp) * 500);
	int remaining = 0;
	int num_steps=200;
	
	if (msgrcv(msgid, &message, sizeof(MessageStruct) - sizeof(long), 1, 0) == -1) {
		perror("msgrcv failed");
		exit(EXIT_FAILURE);
	}
	printMessageDetails(message);
	remaining = processRequests(1, inputFile, shared_mem, msgid, message, remaining, req);
	updateTimestep(msgid, message.timestep);
	
	
	for (int timestep = 2; timestep <= num_steps; timestep++) {
		if (msgrcv(msgid, &message, sizeof(MessageStruct) - sizeof(long), 1, 0) == -1) {
			perror("msgrcv failed");
			exit(EXIT_FAILURE);
		}printf("message.isFinished=%d\n",message.isFinished);
		if(message.isFinished==1){
			printf("Program finished\n");
			free(req);
			free(inputFile);
			return 0;
		}
		printMessageDetails(message);
		
		remaining = processRequests(timestep, inputFile, shared_mem, msgid, message, remaining, req);
		updateTimestep(msgid, message.timestep);
	}
	
	free(req);
	free(inputFile);
	return 0;
}
