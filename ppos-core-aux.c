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

#define AGING -1;
int system_time=0;
// estrutura que define um tratador de sinal (deve ser global ou static)
struct sigaction action;
// estrutura de inicialização to timer
struct itimerval timer;

/*============================================================================================================================*/
// Parte B

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
        if (disk_cmd(DISK_CMD_STATUS, 0, NULL) == 1 && disk.requestQueue) {
            diskrequest_t* request = disk_scheduler();
            if (request) {
                sem_down(&disk.semaforo_queue);
                queue_remove((queue_t**)&disk.requestQueue, (queue_t*)request);
                sem_up(&disk.semaforo_queue);

                if (request->operation == 1) {
                    disk_cmd(DISK_CMD_READ, request->block, request->buffer);
                    disk.livre = 0;
                } else if (request->operation == 2) {
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
    signal(SIGSEGV, clean_exit_on_sig);

    task_create(&disk_mgr_task, disk_mgr_body, NULL);

    return 0;   
}   



int disk_block_read (int block, void *buffer){
    if (sem_down(&disk.semaforo) < 0)
        return -1;
    // cria um request de leitura
    diskrequest_t* request = malloc(sizeof(diskrequest_t));
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


    printf("CHEGOU AQUI!!!!!!!!!!!!\n");
    return 0;
}


int disk_block_write (int block, void *buffer){
    if (sem_down(&disk.semaforo) < 0)
        return -1;

    // cria um request de escrita
    diskrequest_t* request = malloc(sizeof(diskrequest_t));
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


diskrequest_t* disk_scheduler(){
    if(!disk.requestQueue)
        return NULL;
    
    diskrequest_t* request = disk.requestQueue;
    int distancia = abs(request->block- pos_cabeca);

    blocos_percorridos += distancia;
    pos_cabeca = request->block;

    return request;
}


/*============================================================================================================================*/
// Parte A

void tratador()
{
    if(!taskExec){
        exit(-1);
    }
    system_time++;
    if(taskExec!=taskDisp){
        if(taskExec->quantum>0){
            taskExec->quantum--;
            taskExec->processor_time++;
        }

        if(taskExec->quantum<=0){
            task_yield();
        }
    }
}

void temporizador()
{
    // registra a ação para o sinal de timer SIGALRM
    action.sa_handler = tratador ;
    sigemptyset (&action.sa_mask) ;
    action.sa_flags = 0 ;
    if (sigaction (SIGALRM, &action, 0) < 0)
    {
        perror ("Erro em sigaction: ") ;
        exit (1) ;
    }

    // ajusta valores do temporizador
    timer.it_value.tv_usec = 1000;      // primeiro disparo, em micro-segundos
    timer.it_interval.tv_usec = 1000;   // disparos subsequentes, em micro-segundos

    // arma o temporizador ITIMER_REAL (vide man setitimer)
    if (setitimer (ITIMER_REAL, &timer, 0) < 0)
    {
        perror ("Erro em setitimer: ") ;
        exit (1) ;
    }
}


unsigned int systime () {
    return system_time;
}

void age_task(task_t* task) {
    task->dynamicPriority += AGING; // Envelhecimento da tarefa
    if (task->dynamicPriority < -20) {
        task->dynamicPriority = -20; // Limita a prioridade dinâmica
    }
}

task_t * scheduler () {

    task_t *task = readyQueue; //Primeiro elemento da fila de tarefas prontas
    task_t *taskMaxPrio = task;
    // Verifica se a fila de tarefas prontas está vazia
    if (task == NULL) {
        return NULL; 
    }

    int max_priority = 20; // Maior prioridade = -20

    // Procura a tarefa com maior prioridade
    do {
        if (task->dynamicPriority < max_priority) {
            max_priority = task->dynamicPriority;
            taskMaxPrio = task;
        }
        else if (task->dynamicPriority == max_priority) {
            if (task->staticPriority < taskMaxPrio->staticPriority) {
                taskMaxPrio = task; // Se as prioridades dinâmicas forem iguais, escolhe a de maior prioridade estática
            }
        }
        task = task->next;
    }
    while (task != readyQueue);

   //printf("\nscheduler - Tarefa escolhida [%d] - Prioridade dinâmica: %d - Prioridade estática: %d\n", taskMaxPrio->id, taskMaxPrio->dynamicPriority, taskMaxPrio->staticPriority);

   // Para não haver envelhecimento no começo do sistema
    if (taskMaxPrio->id >= 2) {
        task = readyQueue;
        do {
            if (task->id >= 2) {
                age_task(task); // Aplica envelhecimento
            }
            task = task->next;
        }
        while (task != readyQueue);
    }

    //PRINT_READY_QUEUE; // Imprime a fila de tarefas prontas

    //printf("Systemtime: %d\n", system_time);

    // Remove a tarefa da fila de prontas
    (task_t*) queue_remove((queue_t**) &readyQueue, (queue_t*) taskMaxPrio);

    taskMaxPrio->dynamicPriority = taskMaxPrio->staticPriority;
    taskMaxPrio->quantum = 20; // Reseta o quantum da tarefa escolhida
    taskMaxPrio->activations++;
    
    // Retorna a tarefa com maior prioridade
    return taskMaxPrio;
}

void task_setprio (task_t *task, int prio){
    if (task == NULL) {
        task = taskExec; 
    }
    task->staticPriority = prio;
    task->dynamicPriority = prio; 
}

int task_getprio (task_t *task) {
    if (task == NULL) {
        task = taskExec; 
    }
    return task->staticPriority;
}

void before_ppos_init () {
    // put your customization here
#ifdef DEBUG
    printf("\ninit - BEFORE");
#endif
}

void after_ppos_init () {
    // put your customization here
#ifdef DEBUG
    printf("\ninit - AFTER");
#endif
    temporizador();
}

void before_task_create (task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_create - BEFORE - [%d]", task->id);
#endif
}

void after_task_create (task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_create - AFTER - [%d]", task->id);
#endif
    task->staticPriority = 0;
    task->dynamicPriority = 0;
    task->quantum=20;
    task->activations = 0;
    task->processor_time = 0;
    task->begin = systime();
}

void before_task_exit () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_exit - BEFORE - [%d]", taskExec->id);
#endif
    taskExec->end = systime();
    printf("\nTask %d exit: execution time %d ms, processor time %d ms, %d activations\n", 
            taskExec->id, 
            taskExec->end - taskExec->begin, 
            taskExec->processor_time, 
            taskExec->activations); 
}

void after_task_exit () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_exit - AFTER- [%d]", taskExec->id);
#endif
    
}

void before_task_switch ( task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_switch - BEFORE - [%d -> %d]", taskExec->id, task->id);
#endif
    if (taskExec->id == 1 && task->id == 0) {
        taskExec->end = systime();
        printf("\nTask %d exit: execution time %d ms, processor time %d ms, %d activations\n", 
           taskExec->id, 
           taskExec->end - taskExec->begin, 
           taskExec->processor_time, 
           taskExec->activations); 
    }
}

void after_task_switch ( task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_switch - AFTER - [%d -> %d]", taskExec->id, task->id);
#endif

}

void before_task_yield () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_yield - BEFORE - [%d]", taskExec->id);
#endif
}
void after_task_yield () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_yield - AFTER - [%d]", taskExec->id);
#endif
}


void before_task_suspend( task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_suspend - BEFORE - [%d]", task->id);
#endif
}

void after_task_suspend( task_t *task ) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_suspend - AFTER - [%d]", task->id);
#endif
}

void before_task_resume(task_t *task) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_resume - BEFORE - [%d]", task->id);
#endif
}

void after_task_resume(task_t *task) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_resume - AFTER - [%d]", task->id);
#endif
}

void before_task_sleep () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_sleep - BEFORE - [%d]", taskExec->id);
#endif
}

void after_task_sleep () {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_sleep - AFTER - [%d]", taskExec->id);
#endif
}

int before_task_join (task_t *task) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_join - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_task_join (task_t *task) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_join - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}


int before_sem_create (semaphore_t *s, int value) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_create (semaphore_t *s, int value) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_sem_down (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_down - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_down (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_down - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_sem_up (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_up - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_up (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_up - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_sem_destroy (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_sem_destroy (semaphore_t *s) {
    // put your customization here
#ifdef DEBUG
    printf("\nsem_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_create (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_create (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_lock (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_lock - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_lock (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_lock - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_unlock (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_unlock - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_unlock (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_unlock - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mutex_destroy (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mutex_destroy (mutex_t *m) {
    // put your customization here
#ifdef DEBUG
    printf("\nmutex_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_barrier_create (barrier_t *b, int N) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_barrier_create (barrier_t *b, int N) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_barrier_join (barrier_t *b) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_join - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_barrier_join (barrier_t *b) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_join - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_barrier_destroy (barrier_t *b) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_barrier_destroy (barrier_t *b) {
    // put your customization here
#ifdef DEBUG
    printf("\nbarrier_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_create (mqueue_t *queue, int max, int size) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_create - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_create (mqueue_t *queue, int max, int size) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_create - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_send (mqueue_t *queue, void *msg) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_send - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_send (mqueue_t *queue, void *msg) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_send - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_recv (mqueue_t *queue, void *msg) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_recv - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_recv (mqueue_t *queue, void *msg) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_recv - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_destroy (mqueue_t *queue) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_destroy - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_destroy (mqueue_t *queue) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_destroy - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}

int before_mqueue_msgs (mqueue_t *queue) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_msgs - BEFORE - [%d]", taskExec->id);
#endif
    return 0;
}

int after_mqueue_msgs (mqueue_t *queue) {
    // put your customization here
#ifdef DEBUG
    printf("\nmqueue_msgs - AFTER - [%d]", taskExec->id);
#endif
    return 0;
}
