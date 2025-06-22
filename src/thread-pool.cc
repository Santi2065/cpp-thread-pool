/**
 * File: thread-pool.cc
 * --------------------
 * Presents the implementation of the ThreadPool class.
 */

#include "thread-pool.h"
#include <stdexcept>
using namespace std;

// Variable para detectar llamadas a wait() desde workers
thread_local bool ThreadPool::isInsideWorker = false;

// Constructor: Inicializa el pool con numThreads trabajadores
ThreadPool::ThreadPool(size_t numThreads) : 
    wts(numThreads), 
    done(false),
    doneLock(1),
    wtsLock(1),
    queueLock(1),
    taskAvailable(0),
    waitSem(1),
    pendingTasks(0),
    taskCountLock(1) {
    
    // Inicializa cada worker y lo arranca en un thread independiente
    for (size_t i = 0; i < numThreads; i++) {
        wts[i].available = true;
        wts[i].busy = false;
        wts[i].ready = new Semaphore(0);
        wts[i].done = new Semaphore(0);
        wts[i].statusLock = new Semaphore(1);
        wts[i].ts = thread([this, i] { worker(i); });
    }

    // Inicia el thread dispatcher que distribuye tareas
    dt = thread([this] { dispatcher(); });
}

// Programa una nueva tarea para ser ejecutada por un worker
void ThreadPool::schedule(const function<void(void)>& thunk) {
    // Verifica que el pool no esté en proceso de destrucción
    doneLock.wait();
    bool isDone = done;
    doneLock.signal();
    
    if (isDone) {
        throw runtime_error("Cannot schedule tasks after ThreadPool destruction has begun");
    }
    
    if (!thunk) {
        throw runtime_error("Cannot schedule a null function");
    }
    
    // Agrega la tarea a la cola y notifica al dispatcher
    queueLock.wait();
    taskQueue.push(thunk);
    queueLock.signal();

    taskCountLock.wait();
    pendingTasks++;
    taskCountLock.signal();
    
    taskAvailable.signal();
}

// Espera a que todas las tareas programadas terminen
void ThreadPool::wait() {
    // Previene deadlock si se llama desde un worker
    if (isInsideWorker) {
        Semaphore deadlockSem(0);
        deadlockSem.wait(); // Bloqueo permanente
        return;
    }
    
    // Solo un thread puede ejecutar wait() a la vez
    waitSem.wait();
    
    // Espera activa hasta que todas las tareas estén completas
    while (true) {
        taskCountLock.wait();
        size_t remaining = pendingTasks;
        taskCountLock.signal();
        
        if (remaining == 0) break;
        this_thread::yield();
    }
    
    waitSem.signal();
}

// Asigna tareas de la cola a workers disponibles
void ThreadPool::dispatcher() {
    bool isDone = false;
    
    while (!isDone) {
        // Espera a que haya una tarea disponible
        taskAvailable.wait();
        
        // Verifica si el pool está cerrando
        doneLock.wait();
        isDone = done;
        doneLock.signal();
        if (isDone) break;
        
        // Obtiene una tarea de la cola
        function<void(void)> task;
        bool haveTask = false;
        
        queueLock.wait();
        if (!taskQueue.empty()) {
            task = taskQueue.front();
            taskQueue.pop();
            haveTask = true;
        }
        queueLock.signal();
        
        if (!haveTask) continue;
        
        // Busca un worker disponible para asignar la tarea
        bool workerFound = false;
        
        while (!workerFound && !isDone) {
            doneLock.wait();
            isDone = done;
            doneLock.signal();
            
            // Busca secuencialmente un worker disponible
            wtsLock.wait();
            for (size_t i = 0; i < wts.size(); i++) {
                wts[i].statusLock->wait();
                bool isAvailable = wts[i].available && !wts[i].busy;
                
                if (isAvailable) {
                    wts[i].available = false;
                    wts[i].thunk = task;
                    workerFound = true;
                    wts[i].statusLock->signal();
                    wts[i].ready->signal();
                    break;
                }
                wts[i].statusLock->signal();
            }
            wtsLock.signal();
            
            if (!workerFound && !isDone) {
                this_thread::yield();
            }
        }
    }
}

// Ejecuta tareas asignadas por el dispatcher
void ThreadPool::worker(int id) {
    bool isDone = false;
    
    while (!isDone) {
        // Espera hasta que se le asigne una tarea
        wts[id].ready->wait();
        
        // Verifica si el pool está cerrando
        doneLock.wait();
        isDone = done;
        doneLock.signal();
        if (isDone) break;
        
        // Obtiene y marca la tarea como en progreso
        wts[id].statusLock->wait();
        wts[id].busy = true;
        function<void(void)> currentTask = wts[id].thunk;
        wts[id].statusLock->signal();
        
        // Ejecuta la tarea protegiendo contra excepciones
        isInsideWorker = true;
        try {
            currentTask();
        } catch (...) {
            // Ignora excepciones para mantener el worker activo
        }
        isInsideWorker = false;
        
        // Marca el worker como disponible nuevamente
        wts[id].statusLock->wait();
        wts[id].busy = false;
        wts[id].available = true;
        wts[id].statusLock->signal();
        
        // Actualiza el contador de tareas pendientes
        taskCountLock.wait();
        pendingTasks--;
        taskCountLock.signal();
        
        wts[id].done->signal();
    }
}

// Limpia recursos y espera a que terminen todos los threads
ThreadPool::~ThreadPool() {
    // Espera a que terminen las tareas pendientes
    wait();
    
    // Marca el pool como en destrucción
    doneLock.wait();
    done = true;
    doneLock.signal();
    
    // Notifica al dispatcher para que termine
    taskAvailable.signal();
    
    // Despierta a todos los workers para que terminen
    wtsLock.wait();
    for (size_t i = 0; i < wts.size(); i++) {
        wts[i].ready->signal();
    }
    wtsLock.signal();
    
    // Espera a que termine el thread dispatcher
    if (dt.joinable()) {
        dt.join();
    }

    // Espera a que terminen todos los workers y libera recursos
    wtsLock.wait();
    for (size_t i = 0; i < wts.size(); i++) {
        if (wts[i].ts.joinable()) {
            wts[i].ts.join();
        }
        delete wts[i].ready;
        delete wts[i].done;
        delete wts[i].statusLock;
    }
    wtsLock.signal();
    
    // Limpia cualquier tarea pendiente
    queueLock.wait();
    while (!taskQueue.empty()) {
        taskQueue.pop();
    }
    queueLock.signal();
}
