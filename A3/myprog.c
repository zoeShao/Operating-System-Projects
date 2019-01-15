#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#define SIZE 4096

int comp (const void *num1, const void *num2){
  int v1 = *((int*)num1);
  int v2 = *((int*)num2);
  if (v1>v2)  return 1;
  if (v1<v2)  return -1;
  return 0;
}


int main(){
  int i;
  int array1[SIZE];
  int array2[SIZE];

  // Randomly assign number for them.
  srand(time(NULL));
  for (i=0; i<SIZE; i++){
    array1[i] = rand();
  }
  for (i=0; i<SIZE; i++){
    array2[i] = rand();
  }

  // Add array1 to array2
  for (i=0; i<SIZE; i++){
    array2[i] += array1[i];
  }

  // Call c lib qsort on array2
  qsort(array2, sizeof(array2)/sizeof(*array2), sizeof(*array2), comp);

  return 0;
}
