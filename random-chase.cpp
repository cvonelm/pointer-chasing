/*
   Copyright (c) 2016, 2018 Andreas F. Borchert
   All rights reserved.

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
   KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
   WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

/* utility to measure cache and memory read access times */

#include "chase-pointers.hpp"
#include "uniform-int-distribution.hpp"
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <printf.hpp> /* see https://github.com/afborchert/fmt */
extern "C" {
    #include <sched.h>
#include <pthread.h>
}
/* create a cyclic pointer chain that covers all words
   in a memory section of the given size in a randomized order */
void** create_random_chain(std::size_t size)
{
    std::size_t len = size / sizeof(void*);
    void** memory = new void*[len];

    UniformIntDistribution uniform;

    // shuffle indices
    size_t* indices = new std::size_t[len];
    for (std::size_t i = 0; i < len; ++i) {
        indices[i] = i;
    }
    for (std::size_t i = 0; i < len - 1; ++i) {
        std::size_t j = i + uniform.draw(len - i);
        if (i != j) {
            std::swap(indices[i], indices[j]);
        }
    }
    // fill memory with pointer references
    for (std::size_t i = 1; i < len; ++i) {
        memory[indices[i - 1]] = (void*)&memory[indices[i]];
    }
    memory[indices[len - 1]] = (void*)&memory[indices[0]];
    delete[] indices;
    return memory;
}

unsigned int log2(std::size_t val)
{
    unsigned int count = 0;
    while (val >>= 1) {
        ++count;
    }
    return count;
}

#ifndef MIN_SIZE
#define MIN_SIZE 1024 * 1024 * 1024
#endif
#ifndef MAX_SIZE
#define MAX_SIZE 1024 * 1024 * 1024
#endif
#ifndef GRANULARITY
#define GRANULARITY (1u)
#endif
#ifndef NUM_CPUS
#define NUM_CPUS 2
#endif

int num_procs;
std::size_t count;
std::size_t memsize;
void *routine(void *arg)
{
    void** memory = create_random_chain(memsize);
    double t = chase_pointers(memory, count);
    delete[] memory;
    *((double *)arg) = t * 1000000000 / count;
    return NULL;
}
int main()
{
    std::vector<pthread_t> threads(NUM_CPUS);
    std::vector<double> times(NUM_CPUS);
    fmt::printf("   memsize  time in ns\n");
    for (memsize = MIN_SIZE; memsize <= MAX_SIZE;
         memsize += (std::size_t { 1 }
             << (std::max(GRANULARITY, log2(memsize)) - GRANULARITY))) {

        count = std::max(memsize * 16, std::size_t { 1 } << 30);
        num_procs = NUM_CPUS;
        
        for(std::size_t i = 0; i < NUM_CPUS; i++)
        {
            cpu_set_t t;
            pthread_attr_t attr;
            
            CPU_ZERO(&t);
            CPU_SET(i, &t);
            std::cout << "MEEEEP" << std::endl; 
            pthread_attr_init(&attr);
            pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &t);
            pthread_create(&threads[i], &attr, &routine, &times[i]);
        }
        for(pthread_t thread: threads)
        {
            pthread_join(thread, NULL);
        }
        auto lambda = [&](double a, double b){return a + b / times.size(); };    
        double mean_ns = std::accumulate(times.begin(), times.end(), 0.0, lambda);
        
        fmt::printf(" %9u  %10.5lf\n", memsize, mean_ns);
        std::cout.flush();   
    }
}
