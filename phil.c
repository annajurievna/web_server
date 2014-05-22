#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

int n = 50;
int eat_count = 10;
pthread_mutex_t forks[4];

typedef struct Phil {
    pthread_t thread;
    pthread_mutex_t *left, *right;
    int name, l, r;
    double time_of_waiting;
} Ph;

void *routine( void* a ) {
    Ph *current = (Ph *) a;
    for (int i = 0; i < eat_count; ++i) {
        time_t beg, end;
        time(&beg);
        while ( pthread_mutex_trylock( current->left ) != 0 ) {
            sleep(rand() % 3 + 1);
            printf( "Phil %d tried to take his left fork to eat for the %d time\n", current->name, i + 1);
        }
        while ( pthread_mutex_trylock( current->right ) != 0 ) {
            sleep(rand() % 3 + 1);
            printf( "Phil %d tried to take his right fork to eat for the %d time\n", current->name, i + 1);
        }
        time(&end);
        current->time_of_waiting += difftime(end, beg);
        printf( "Phil %d is eating for the %d time \n", current->name, i + 1);
        sleep(rand() % 3 + 1);
        pthread_mutex_unlock( current->left );
        pthread_mutex_unlock( current->right );
        printf( "Phil %d dropped his forks after eating for the %d time\n", current->name, i + 1);
    }
    return 0;
}

void algorithm( ) {
    Ph philosophers[n];
    Ph *current;
    for (int  i = 0; i < n; i++ ) {
        current = &philosophers[i];
        current->name = i;
        if ( rand() % 2 ) {
            current->left = &forks[i];
            current->right = &forks[ (i + 1) % n ];
            current->l = i;
            current->r = (i+1) % n;
        }
        else {
            current->right = &forks[i];
            current->left = &forks[ (i + 1) % n ];
            current->r = i;
            current->l = (i+1) % n;
        }
        pthread_create( &current->thread, NULL, routine, current );
    }

    for (int i = 0; i < n; i++ ) {
        current = &philosophers[i];
        pthread_join( current->thread, NULL );
    }

    for (int i = 0; i < n; ++i) {
        Ph *current = &philosophers[i];
        printf("Phil %d has been waiting for %.f seconds\n", current -> name, current -> time_of_waiting );
    }
}


int main() {
    algorithm();
    return 0;
}
