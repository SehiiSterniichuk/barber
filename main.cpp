#include <iostream>
#include "string"
#include "pthread.h"
#include <unistd.h>
#include <condition_variable>

#define CLIENT_MIN_WORK_TIME 1800
#define CLIENT_MAX_WORK_TIME 2500
#define BARBER_MIN_WORK_TIME 1
#define BARBER_MAX_WORK_TIME 10
#define MAX_QUEUE_CAPACITY 5
#define NUMBER_OF_CLIENTS 8
#define WORK_TIME 8000
#define NOTHING -2147483647

using namespace std;

bool isWork = true; //прапор роботи програми. Поки true програма працює
mutex console;//забезпечує монопольний доступ до консолі
mutex queueClientSection;//лише один клієнт може додаватися у чергу
mutex counterSection;//забезпечує монопольний доступ до змінної розміру черги


class event {//структура для роботи з подіями
private:
    condition_variable cv;
    /*Клас condition_variable — це примітив синхронізації,
 * який можна використовувати для блокування потоку або кількох потоків одночасно,
 * поки інший потік не змінить спільну змінну (умову) і не повідомить про condition_variable.*/
    mutex mtx;//мютекс потрібний для condition_variable
public:
    void wait() {
        unique_lock<mutex> localMutex(mtx);
        cv.wait(localMutex);
    }

    void notifyAll() {
        cv.notify_all();
    };
};

event fullQueue;//подія повної черги
event emptyQueue;// подія пустої черги
event clientEvent[NUMBER_OF_CLIENTS];/* події клієнтів: записався у чергу - поток клієнта чекає
 * барбер постриг клієнта - будимо поток клієнта*/

int counter = 0;// поточний розмір черги

struct Node {
    Node *prev;
    Node *next;
    int data;

    Node(Node *prev, Node *next, const int &data) {
        this->prev = prev;
        this->data = data;
        this->next = next;
    }
};


class Queue {
private:
    Node *head = NULL;
    Node *tail = NULL;
public:
    void push(const int &data) {
        if (head == NULL) {
            head = new Node(NULL, NULL, data);
            tail = head;
            return;
        }
        Node *newbie = new Node(tail, NULL, data);
        tail->next = newbie;
        newbie->prev = tail;
        tail = newbie;
    }

    int pop() {
        if (tail == NULL) {
            return NOTHING;
        }
        int data = tail->data;
        Node *oldNode = tail;
        tail = tail->prev;
        if (NULL != tail) {
            tail->next = NULL;
        } else {
            head = NULL;
        }
        delete oldNode;
        return data;
    }

    void printAll() {//вивід всіх елементів черги у консоль
        Node *current = head;
        console.lock();//забрали доступ до консолі
        cout << "Queue == [ ";
        while (current != NULL) {
            int data = current->data;
            cout << data << ", ";
            current = current->next;
        }
        cout << "]" << endl;
        console.unlock();//повернули доступ до консолі
    }

    ~Queue() {
        while (head != NULL) {
            pop();
        }
    }
} queue;//черга клієнтів, що записалися

void sleepInRange(int min, int max);//забезпечує сон потоку випадковий час на проміжку min and max

void Sleep(int time);//сон потоку у мілісекундах

void print(const string &message);//вивід інформації у консоль. Виводити одночасно може лише один потік

int push(const int &id);//спроба записати клієнта у чергу

int get();//спроба дістати клієнта з черги

void *barber(void *args) {
    int client = NOTHING;//індекс потоку клієнта
    while (isWork) {
        client = get();//дістаємо індекс клієнта.
        if(client == NOTHING){continue;}
        /*УВАГА цикл не робить марної роботи коли немає клієнтів,
         * бо барбер засинає у функції get() коли черга пуста! */
        print("Barber is serving client: " + to_string(client));//почали обслуговувати клієнта
        sleepInRange(BARBER_MIN_WORK_TIME, BARBER_MAX_WORK_TIME);
        print("Barber has finished to serve client: " + to_string(client));//закінчили обслуговувати клієнта
        clientEvent[client].notifyAll();// будимо клієнта, щоб він покинув перукарню
    }
}

int get() {
    counterSection.lock();//блокуємо доступ до змінної counter для інших потоків
    int newID;
    if (counter == 0) {
        print("Queue is empty. Barber has started sleeping");
        counterSection.unlock();
        emptyQueue.wait();//чекаємо поки якийсь клієнт не стане у чергу
        print("Barber has woken up");
        return NOTHING;
    } else if (counter == MAX_QUEUE_CAPACITY) {
        newID = queue.pop();
        counter--;
        counterSection.unlock();
        fullQueue.notifyAll();// "будимо" клієнта, який очікував, тому що черга була повна
    } else if (counter == 1) {
        newID = queue.pop();
        counter--;
        counterSection.unlock();
    } else {
        counter--;
        counterSection.unlock();
        //можна повертати доступ до змінної, тому що барбер і клієнти модифікують різні кінці черги, бо елемент не один у черзі
        newID = queue.pop();
    }
    queue.printAll();//виводимо на екран усі елементи черги
    return newID;
}

void *client(void *args) {
    int id = *(int *) args;//індекс потоку клієнта integer
    srand(id);
    /*зерно, яке еквівалентне індексу потоку,що у свою чергу забезпечує
     * різні випадкові значення у різних потоках*/
    sleepInRange(0, CLIENT_MAX_WORK_TIME);
    int res = NOTHING / 2;
    while (isWork) {
        if(res != NOTHING){sleepInRange(CLIENT_MIN_WORK_TIME, CLIENT_MAX_WORK_TIME);}
        queueClientSection.lock();//забираємо доступ до кінця черги у інших клієнтів
        res = push(id);
        if(res == NOTHING){continue;}
        queueClientSection.unlock();//повертаємо доступ до кінця черги для інших клієнтів
        print("Client: " + to_string(id) + " is waiting for barber");
        clientEvent[id].wait();//клієнт очікує поки його не постриже барбер
    }
}

int push(const int &id) {
    counterSection.lock();// блокуємо доступ до змінної counter для інших потоків
    if (counter == MAX_QUEUE_CAPACITY) {
        counterSection.unlock();// повертаємо доступ до змінної counter для інших потоків
        print("Queue is full. Client " + to_string(id) + " is waiting on the street");
        fullQueue.wait(); //чекаємо поки барбер не почне стригти когось
        return NOTHING;
    } else if (counter == 0) {
        queue.push(id);
        counter++;
        counterSection.unlock();
        emptyQueue.notifyAll();//будимо барбера, який заснув коли черга була пуста
    } else if (counter == 1) {
        queue.push(id);
        counter++;
        counterSection.unlock();
    } else {
        counter++;
        counterSection.unlock();
        //можна повертати доступ до змінної, тому що споживач з виробником модифікують різні кінці черги, бо елемент не один у черзі
        queue.push(id);
    }
    queue.printAll();//виводимо на екран усі елементи черги
    return id;
}

int main() {
    pthread_t barberThread;
    pthread_create(&barberThread, NULL, barber, NULL);
    Sleep(BARBER_MAX_WORK_TIME);
    pthread_t clients[NUMBER_OF_CLIENTS];
    int clientId[NUMBER_OF_CLIENTS];
    for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        clientId[i] = i;
        pthread_create(&clients[i], NULL, client, &clientId[i]);
    }
    Sleep(WORK_TIME);
    isWork = false;//барбер та клієнти мають виходити з циклів та завершувати роботу потоків
    print("isWork == false");
    struct timespec ts;
    clock_gettime(CLIENT_MAX_WORK_TIME, &ts);
    for (int i = 0; i < NUMBER_OF_CLIENTS; ++i) {
        //джоінимо потоки клієнтів, для цього ми будемо їх будити, якщо вони сплять у циклі while
        clientEvent[i].notifyAll();
        while (pthread_timedjoin_np(clients[i],NULL, &ts) == ETIMEDOUT){
            /*pthread_timedjoin_np - еквівалентна функції pthread_join(),
             * за винятком того, що вона повертає ETIMEDOUT,
             * якщо цільовий потік не завершується до закінчення
             * зазначеного абсолютного часу. У даному випадку до часу  CLIENT_MAX_WORK_TIME */
            fullQueue.notifyAll();
            clientEvent[i].notifyAll();
        }
    }
    print("Clients have finished work");
    clock_gettime(BARBER_MAX_WORK_TIME, &ts);
    emptyQueue.notifyAll();
    while (pthread_timedjoin_np(barberThread,NULL, &ts) == ETIMEDOUT){
        //робимо те саме, що і з клієнтами вище
        emptyQueue.notifyAll();
    }
    print("Barber has finished work");
    return 0;
}

void sleepInRange(int min, int max) {
    int time = rand() % (max - min + 1) + min;
    Sleep(time);
}

void Sleep(int time) {//мілісекунди
    usleep(time * 1000);// usleep працює у мікросекундах, тому ми домножуємо на 1000
}

void print(const string &message) {
    console.lock();//забираємо доступ до консолі
    cout << message << endl;
    console.unlock();//повертаємо доступ до консолі
}
