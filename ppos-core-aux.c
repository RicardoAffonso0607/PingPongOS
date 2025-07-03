#include "ppos.h"
#include "ppos-core-globals.h"
#include "ppos-disk-manager.h"

#include <signal.h>
#include <sys/time.h>
#include <errno.h>
#include "disk-driver.h"

// ****************************************************************************
// Adicione TUDO O QUE FOR NECESSARIO para realizar o seu trabalho
// Coloque as suas modificações aqui, 
// p.ex. includes, defines variáveis, 
// estruturas e funções
//
// ****************************************************************************
#include <signal.h>
#include <sys/time.h>
#include <limits.h>
#include "disk-driver.h"
#define ALPHA -1
#define QUANTUM 20
unsigned int _systemTime = 0;
unsigned int dispatcher_activation_count = 0; //  Alternativas pois não é possível acessar as variáveis
unsigned int dispatcher_last_activation_time = 0;// do dispatcher caso a fila não esteja vazia.
unsigned int dispatcher_create_time = 0;
unsigned int dispatcher_processor_time = 0;
unsigned int dispatcher_execution_time = 0;
int its_first_time = 0;

// estrutura que define um tratador de sinal (deve ser global ou static)
struct sigaction action;
// estrutura de inicialização to timer
struct itimerval timer;

/*============================================================================================================================*/
// Parte B

#define ESCALONAMENTO 'F'
//#define ESCALONAMENTO 'S'
//#define ESCALONAMENTO 'C'

#include <limits.h>

disk_t disk; // variavel global que representa o disk do SO   

task_t disk_mgr_task;

struct sigaction d_action;

int pos_cabeca = 0;
int blocos_percorridos = 0;

void disk_mgr_body (){
    while (1) {
        sem_down(&disk.semaforo);
        if (disk.sinal) {
            disk.sinal = 0;
            task_resume(disk.diskQueue);    
            disk.livre = 1;
        }
        if (disk.livre) {
            diskrequest_t* request = disk_scheduler();
            if (request) {
                sem_down(&disk.semaforo_queue);
                queue_remove((queue_t**)&disk.requestQueue, (queue_t*)request);
                sem_up(&disk.semaforo_queue);

                if (request->operation == DISK_CMD_READ) {
                    disk_cmd(DISK_CMD_READ, request->block, request->buffer);
                    disk.livre = 0;
                } else if (request->operation == DISK_CMD_WRITE) {
                    disk_cmd(DISK_CMD_WRITE, request->block, request->buffer);
                    disk.livre = 0;
                }
                free(request);  
            }
        }
        sem_up(&disk.semaforo);
        task_yield();
    }
}

void diskSignalHandler(int signum) {
    disk.sinal = 1;
}

// Handler para SIGSEGV
void clean_exit_on_sig(int sig_num) {
    int err = errno;
    fprintf(stderr, "\n ERROR[Signal = %d]: %d \"%s\"\n", sig_num, err, strerror(err));
    exit(err);
}

int disk_mgr_init (int *numBlocks, int *blockSize){
    // inicializando o disk virtual
    disk_cmd (DISK_CMD_INIT, 0, 0);

    //consulta o tamanho do bloco e do disk
    *numBlocks = disk_cmd (DISK_CMD_DISKSIZE, 0, 0);
    *blockSize = disk_cmd (DISK_CMD_BLOCKSIZE, 0, 0);
    
    if(disk_cmd (DISK_CMD_STATUS, 0, 0)==0|| *numBlocks<0 || *blockSize<0)
        return 1;

    //inicializando o disk do SO
    disk.numBlocks = *numBlocks;
    disk.blockSize = *blockSize;
    disk.sinal = 0;
    disk.livre = 1;
    disk.diskQueue = NULL;
    disk.requestQueue = NULL;
    sem_create(&disk.semaforo,1); // inicializa o semaforo
    sem_create(&disk.semaforo_queue,1);

    // Handler do sinal do disk
    d_action.sa_handler = diskSignalHandler;
    sigemptyset(&d_action.sa_mask);   
    d_action.sa_flags = 0;
    if (sigaction(SIGUSR1, &d_action, NULL) < 0) {
        perror("Erro em sigaction: ");
        exit(1);
    }
    //signal(SIGSEGV, clean_exit_on_sig);

    task_create(&disk_mgr_task, disk_mgr_body, NULL);
    countTasks--;

    return 0;   
}   



int disk_block_read (int block, void *buffer){
    if (sem_down(&disk.semaforo) < 0)
        return -1;
    // cria um request de leitura
    diskrequest_t* request = (diskrequest_t*)malloc(sizeof(diskrequest_t));
    request->operation = DISK_CMD_READ; // leitura
    request->block = block;
    request->buffer = buffer;
    request->next = request->prev = NULL;
    request->task = taskExec;

    // coloca o request na fila
    sem_down(&disk.semaforo_queue);
    queue_append((queue_t**)&disk.requestQueue, (queue_t*)request);
    sem_up(&disk.semaforo_queue);
    

    sem_up(&disk.semaforo);

    task_suspend(taskExec, &disk.diskQueue);
    task_yield();

    return 0;
}


int disk_block_write (int block, void *buffer){
    if (sem_down(&disk.semaforo) < 0)
        return -1;

    // cria um request de escrita
    diskrequest_t* request = (diskrequest_t*)malloc(sizeof(diskrequest_t));
    request->operation = DISK_CMD_WRITE; // escrita
    request->block = block;
    request->buffer = buffer;
    request->next = request->prev = NULL;
    request->task = taskExec;

    // coloca o request na fila
    sem_down(&disk.semaforo_queue);
    queue_append((queue_t**)&disk.requestQueue, (queue_t*)request);
    sem_up(&disk.semaforo_queue);

    // faz a tarefa executar se ela estava suspensa
    if (disk_mgr_task.state == 'S')
        task_resume(&disk_mgr_task);

    sem_up(&disk.semaforo);

    task_suspend(taskExec, &disk.diskQueue);
    task_yield();

    return 0;
}


diskrequest_t* disk_scheduler_fcfs(){
    if(!disk.requestQueue)
        return NULL;
    
    diskrequest_t* request = disk.requestQueue;
    int distancia = abs(request->block- pos_cabeca);

    blocos_percorridos += distancia;
    pos_cabeca = request->block;

    return request;
}

diskrequest_t* disk_scheduler_sstf() {
    if (!disk.requestQueue)
        return NULL;

    diskrequest_t* first = disk.requestQueue;
    diskrequest_t* sst = first;

    int distancia = 0;
    int min_dist = abs(sst->block - pos_cabeca);

    diskrequest_t* request = first;
    do {
        distancia = abs(request->block - pos_cabeca);
        if(distancia < min_dist){
            min_dist = distancia;
            sst = request;
        }
        /*printf("SSTF: Verificando bloco %d, distância: %d\n",
               request->block, distancia);*/
        request = request->next;
    } while (request != first);

    blocos_percorridos += min_dist;
    pos_cabeca = sst->block;

    printf("SSTF: Selecionado bloco %d, distância: %d, blocos percorridos: %d\n",
           sst->block, min_dist, blocos_percorridos);

    return sst;
}

diskrequest_t* disk_scheduler_cscan() {
    if (!disk.requestQueue)
        return NULL;

    diskrequest_t* first_elem = disk.requestQueue;
    diskrequest_t* next_request = first_elem;
    diskrequest_t* cscan = NULL;
    diskrequest_t* min_block_ptr = NULL;
    int short_dist_block_pos = INT_MAX;
    int min_block_pos = INT_MAX;
    int dist = 0;

    do {
        if (min_block_pos > next_request->block) {
            min_block_pos = next_request->block;
            min_block_ptr = next_request;
        }
        if (next_request->block >= pos_cabeca && next_request->block < short_dist_block_pos) {
            short_dist_block_pos = next_request->block;
            cscan = next_request;
        }
        next_request = next_request->next;
    } while (next_request != first_elem);

    if (cscan == NULL) {
        cscan = min_block_ptr;
        dist = ((disk.numBlocks - 1) - pos_cabeca) + min_block_pos + disk.numBlocks;
        blocos_percorridos += dist;
        pos_cabeca = min_block_pos;
    }
    else {
        dist = abs(pos_cabeca - short_dist_block_pos);
        blocos_percorridos += dist;
        pos_cabeca = short_dist_block_pos;
    }

    printf("CSCAN: Selecionado bloco %d, distância: %d, blocos percorridos: %d\n",
           cscan->block, dist, blocos_percorridos);

    return cscan;
}

diskrequest_t* disk_scheduler(){
    if(ESCALONAMENTO == 'F')
        return disk_scheduler_fcfs();
    else if(ESCALONAMENTO == 'S')
        return disk_scheduler_sstf();
    else if(ESCALONAMENTO == 'C')
        return disk_scheduler_cscan();
    return NULL;
}


/*============================================================================================================================*/
// Parte A

// tratador do sinal
void tick_handler (int signum)
{
    _systemTime++; // ms
    if(preemption == 1){
        if(taskExec->id != 1){
            taskExec->ticks--;
            if(taskExec->ticks == 0){
                task_yield();
            }
        }
    }
}

task_t* scheduler (){
    if(readyQueue == NULL){
        printf("Fila de tarefas prontas vazia\n");
        return NULL;
    }
    else if(readyQueue == readyQueue->next){
        return readyQueue;
    }

    task_t *first = readyQueue;
    int maxPrio = 21;
    task_t *taskMaxPrio = NULL;
    task_t *task = first;

    do{
        if(task->prio_d < maxPrio){
            maxPrio = task->prio_d;
            taskMaxPrio = task;
        }
        task = task->next;
    }while(task!=first);

    task = first;

    do{
        if(task != taskMaxPrio){
            task->prio_d = task->prio_d + ALPHA;
        }
        task = task->next;
    }while(task!=first);

    taskMaxPrio->prio_d = taskMaxPrio->prio_s;
    return taskMaxPrio;
}

void task_setprio (task_t *task, int prio){
    if(prio < -20 || prio > 20){
        printf("Prioridade fora do intervalo permitido\n");
    }
    if(task == NULL){
        taskExec->prio_s = prio;
        taskExec->prio_d = prio;
    }
    else{
        task->prio_s = prio;
        task->prio_d = prio;
    }
}

int task_getprio (task_t *task){
    if(task == NULL) {
        return taskExec->prio_s;
    }
    return task->prio_s;
}

unsigned int systime (){
    return _systemTime;
}

void before_ppos_init () {
#ifdef DEBUG
    printf("\ninit - BEFORE");
#endif
}

void after_ppos_init () {
    task_setprio(taskMain, 0);
    // Código adaptado do arquivo timer.c
    // registra a ação para o sinal de timer SIGALRM
    action.sa_handler = tick_handler ;
    sigemptyset (&action.sa_mask) ;
    action.sa_flags = 0 ;
    if (sigaction (SIGALRM, &action, 0) < 0)
    {
        perror ("Erro em sigaction: ") ;
        exit (1) ;
    }

    // ajusta valores do temporizador
    timer.it_value.tv_usec = 1000;      // primeiro disparo, em micro-segundos
    timer.it_value.tv_sec  = 0;      // primeiro disparo, em segundos
    timer.it_interval.tv_usec = 1000;   // disparos subsequentes, em micro-segundos
    timer.it_interval.tv_sec  = 0;   // disparos subsequentes, em segundos

    // arma o temporizador ITIMER_REAL (vide man setitimer)
    if (setitimer (ITIMER_REAL, &timer, 0) < 0)
    {
        perror ("Erro em setitimer: ") ;
        exit (1) ;
    }
#ifdef DEBUG
    printf("\ninit - AFTER");
#endif
}

void before_task_create (task_t *task ){
#ifdef DEBUG
    printf("\ntask_create - BEFORE - [%d]", task->id);
#endif
}

void after_task_create(task_t *task){
    task_setprio(task,0);
    if(task->id == 1){
        its_first_time = 1;
        dispatcher_create_time = _systemTime;
    }
    else{
        task->create_time = _systemTime;
        task->activation_count = 0;
        task->processor_time = 0;
        task->execution_time = 0;
        task->last_activation_time = 0;
    }


#ifdef DEBUG
    printf("\ntask_create - AFTER - [%d]", task->id);
#endif

}

void before_task_exit () {
#ifdef DEBUG
    printf("\ntask_exit - BEFORE - [%d]", taskExec->id);
#endif
}

void after_task_exit () {
    if(taskExec->id == 1){
        dispatcher_processor_time += (_systemTime - dispatcher_last_activation_time);
        dispatcher_execution_time = _systemTime - dispatcher_create_time;
        printf("Task %d exit: execution time %u ms, processor time %u ms, %u activations\n",
        taskExec->id, dispatcher_execution_time, dispatcher_processor_time, dispatcher_activation_count);
        its_first_time = 0;
    }
    else{
        taskExec->execution_time = _systemTime - taskExec->create_time;
        printf("Task %d exit: execution time %u ms, processor time %u ms, %u activations\n",
        taskExec->id, taskExec->execution_time, taskExec->processor_time, taskExec->activation_count);
    }
#ifdef DEBUG
    printf("\ntask_exit - AFTER- [%d]", taskExec->id);
#endif
}

void before_task_switch ( task_t *task ) {
    if(task->id != 1){
        task->ticks = QUANTUM;
    }
    if(taskExec->id == 1){
        dispatcher_processor_time += (_systemTime - dispatcher_last_activation_time);
        its_first_time = 0;
    }
#ifdef DEBUG
    printf("\ntask_switch - BEFORE - [%d -> %d]", taskExec->id, task->id);
#endif
}

void after_task_switch ( task_t *task ) {
    if (task->id != 1){
        task->last_activation_time = _systemTime;
        task->activation_count++;
    }
    else{
        dispatcher_last_activation_time = _systemTime; // Se é o dispatcher, atualiza o tempo de última ativação
        dispatcher_activation_count++; // e a contagem de ativações com as variáveis globais.
    }
#ifdef DEBUG
    printf("\ntask_switch - AFTER - [%d -> %d]", taskExec->id, task->id);
#endif
}

void before_task_yield () {
#ifdef DEBUG
    printf("\ntask_yield - BEFORE - [%d]", taskExec->id);
#endif
}

void after_task_yield () {
    taskExec->processor_time += (_systemTime - taskExec->last_activation_time);
#ifdef DEBUG
    printf("\ntask_yield - AFTER - [%d]", taskExec->id);
#endif
}


void before_task_suspend( task_t *task ) {
#ifdef DEBUG
    printf("\ntask_suspend - BEFORE - [%d]", task->id);
#endif
}

void after_task_suspend( task_t *task ) {
#ifdef DEBUG
    printf("\ntask_suspend - AFTER - [%d]", task->id);
#endif
}

void before_task_resume(task_t *task) {
#ifdef DEBUG
    printf("\ntask_resume - BEFORE - [%d]", task->id);
#endif
}

void after_task_resume(task_t *task) {
#ifdef DEBUG
    printf("\ntask_resume - AFTER - [%d]", task->id);
#endif
}

void before_task_sleep () {
#ifdef DEBUG
    printf("\ntask_sleep - BEFORE - [%d]", taskExec->id);
#endif
}

void after_task_sleep () {
#ifdef DEBUG
    printf("\ntask_sleep - AFTER - [%d]", taskExec->id);
#endif
}

int before_task_join (task_t *task) {
#ifdef DEBUG
    printf("\ntask_join - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_task_join (task_t *task) {
#ifdef DEBUG
    printf("\ntask_join - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}


int before_sem_create (semaphore_t *s, int value) {
#ifdef DEBUG
    printf("\nsem_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_create (semaphore_t *s, int value) {
#ifdef DEBUG
    printf("\nsem_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_sem_down (semaphore_t *s) {
#ifdef DEBUG
    printf("\nsem_down - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_down (semaphore_t *s) {
#ifdef DEBUG
    printf("\nsem_down - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_sem_up (semaphore_t *s) {
#ifdef DEBUG
    printf("\nsem_up - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_up (semaphore_t *s) {
#ifdef DEBUG
    printf("\nsem_up - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_sem_destroy (semaphore_t *s) {
#ifdef DEBUG
    printf("\nsem_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_destroy (semaphore_t *s) {
#ifdef DEBUG
    printf("\nsem_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_create (mutex_t *m) {
#ifdef DEBUG
    printf("\nmutex_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_create (mutex_t *m) {
#ifdef DEBUG
    printf("\nmutex_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_lock (mutex_t *m) {
#ifdef DEBUG
    printf("\nmutex_lock - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_lock (mutex_t *m) {
#ifdef DEBUG
    printf("\nmutex_lock - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_unlock (mutex_t *m) {
#ifdef DEBUG
    printf("\nmutex_unlock - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_unlock (mutex_t *m) {
#ifdef DEBUG
    printf("\nmutex_unlock - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_destroy (mutex_t *m) {
#ifdef DEBUG
    printf("\nmutex_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_destroy (mutex_t *m) {
#ifdef DEBUG
    printf("\nmutex_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_barrier_create (barrier_t *b, int N) {
#ifdef DEBUG
    printf("\nbarrier_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_barrier_create (barrier_t *b, int N) {
#ifdef DEBUG
    printf("\nbarrier_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_barrier_join (barrier_t *b) {
#ifdef DEBUG
    printf("\nbarrier_join - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_barrier_join (barrier_t *b) {
#ifdef DEBUG
    printf("\nbarrier_join - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_barrier_destroy (barrier_t *b) {
#ifdef DEBUG
    printf("\nbarrier_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_barrier_destroy (barrier_t *b) {
#ifdef DEBUG
    printf("\nbarrier_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_create (mqueue_t *queue, int max, int size) {
#ifdef DEBUG
    printf("\nmqueue_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_create (mqueue_t *queue, int max, int size) {
#ifdef DEBUG
    printf("\nmqueue_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_send (mqueue_t *queue, void *msg) {
#ifdef DEBUG
    printf("\nmqueue_send - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_send (mqueue_t *queue, void *msg) {
#ifdef DEBUG
    printf("\nmqueue_send - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_recv (mqueue_t *queue, void *msg) {
#ifdef DEBUG
    printf("\nmqueue_recv - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_recv (mqueue_t *queue, void *msg) {
#ifdef DEBUG
    printf("\nmqueue_recv - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_destroy (mqueue_t *queue) {
#ifdef DEBUG
    printf("\nmqueue_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_destroy (mqueue_t *queue) {
#ifdef DEBUG
    printf("\nmqueue_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_msgs (mqueue_t *queue) {
#ifdef DEBUG
    printf("\nmqueue_msgs - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_msgs (mqueue_t *queue) {
#ifdef DEBUG
    printf("\nmqueue_msgs - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}