#
# Define k random distrobutions centered around random positions
# Keep track of empty cells
# For each set
#   Until you have placed everything
#     Randomly pull an empty cell
#     Compute the current PDF value of this cell for this distribution
#     sum-up the probability for all already occupied cells and then scale your
#     current p with 1 / (1-sum)
#     Pull uniform random number [0,1]
#     Accept or reject sample
#

from sys import argv,exit
import numpy as np
from math import *
import random 

symbol = ["ro","g^","bs"]
colors = ["r","g","b"]

def n_choose_k(n,k):
    return factorial(n) / (factorial(k)*factorial(n-k))


# Base class for are probability distribution 
class Distribution:
    
    def __init__(self,total,center):
        self.size = total
        self.center = center
        self.fill_sum = 0 # SUm of PMF of filled slots
                 
    # Shift the index i according to the center
    def shift(self,i):
        if abs(self.center - i) > self.size/2:
            if i < self.center:
                i += self.size
            else:
                i -= self.size
        
        return self.size/2 - (self.center - i)
    
    def pmf(self,i):
        i = self.shift(i)

        return self.centered_pmf(i)
    
    # Adjusted pmf by filled slot
    def adjustedPMF(self,i):
        pmf = self.pmf(i)
        
        return pmf / (1-self.fill_sum)
    
    def fillSlot(self,i):
        self.fill_sum += self.pmf(i)
        
        
    
class Binomial(Distribution):
    
    def __init__(self,total,center,p):
        Distribution.__init__(self,total,center)
        
        self.p = p
    
    def centered_pmf(self,i):
        return n_choose_k(self.size, i)*pow(self.p,i)*pow(1-self.p,self.size-i)
            


if len(argv) < 0:
    print "Usage: %s <total number of nodes> <#nodes task 1> .... <#nodes task N>"
    exit(0)
    

# Parse the command line
system_size = int(argv[1])
task_sizes = [int(arg) for arg in argv[2:]]

# Create a list of tasks
tasks = range(0,len(task_sizes))

# Shuffle the tasks to give everyone the opportunity to have an "empty" machine
np.random.shuffle(tasks)

# Adjust the order of sizes
task_sizes = [task_sizes[i-1] for i in tasks ]

# Create random array of centers
task_centers = np.random.random_integers(0,system_size-1,len(tasks))

# Create the corresponding distributions
task_distributions = [Binomial(system_size,c,0.5) for c in task_centers]

# Slots
nodes = np.zeros(system_size)

# List of empty slots
empty = list(xrange(0,system_size))

# For all tasks
for t,size,dist in zip(tasks,task_sizes,task_distributions):
    #print "Started task ", i, size
    count = 0
    while count < size:
        
        # Choose a random node
        elem = random.choice(empty)
        
        # Get a uniform random number 
        test = np.random.uniform()
        
        # Get the current pmf value for the distribution
        current = dist.adjustedPMF(elem)

        # If we pass the test
        if test < current:
            #print "Picked ", elem
            # Assign the node to this task
            nodes[elem] = t+1
            count += 1
            
            # Remove the node from the empty list
            empty.remove(elem)
            
            # Adjust all distributions to include another filled element
            for d in task_distributions:
                d.fillSlot(elem)
                

pmfs = []
for d in task_distributions:
    pmfs.append([d.pmf(i) for i in xrange(0,system_size)])

import matplotlib.pyplot as plt


fig, ax = plt.subplots()
for pmf,t in zip(pmfs,tasks):
    #print "Colors ", colors[t]
    ax.plot(pmf,colors[t])

print ""
for t in tasks:
    #print "Colors ", symbol[t]
    x = np.where(nodes == t+1)
    ax.plot(x,[(t+1)*0.05]*len(x),symbol[t])
    #print x

plt.show()

