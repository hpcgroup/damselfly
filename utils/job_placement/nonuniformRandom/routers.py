#!/usr/bin/env python
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
import struct
from math import *
import random 
from __builtin__ import True

symbol = ["ro","g^","bs","yo","cs"]
colors = ["r","g","b","y","c"]

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
        
        if abs(1 - self.fill_sum) < 1e-8:
            return 1
        else:
            return pmf / (1-self.fill_sum)
    
    def fillSlot(self,i):
        self.fill_sum += self.pmf(i)
        #print "Filling slot ", i, " " , self.pmf(i), " ", self.fill_sum 
       
        
    
class Binomial(Distribution):
    
    def __init__(self,total,center,p):
        Distribution.__init__(self,total,center)
        
        self.p = p
    
    def centered_pmf(self,i):
        return n_choose_k(self.size, i)*pow(self.p,i)*pow(1-self.p,self.size-i)
            
class Geometric(Distribution):
    
    def __init__(self,total,center,p):
        Distribution.__init__(self, total, center)
        
        self.p = p
        
    def shift(self, i):
        return abs(i-self.center)
        
    def centered_pmf(self,i):
        # Total mass of two geoemtrics attached at the center
        total_mass = 2 - self.p
        
        return (1-self.p)**(i)*self.p / total_mass
        

def rank_to_coords(rank,groups,rows,columns,nodes_per_router,cores_per_node):

    dims = [0,0,0,0,rank]
    dims[4] = rank % cores_per_node;
    rank /= cores_per_node;

    dims[3] = rank % nodes_per_router;
    rank /= nodes_per_router;

    dims[2] = rank % columns;
    rank /= columns;

    dims[1] = rank % rows;
    rank /= rows;

    dims[0] = rank % groups;
    
    return dims


if len(argv) < 10:
    print "Usage: %s <numGroups> <numRows> <numColumns> <numNodesPerRouter> <numCoresPerNode>  [Binomial|Geometric] <p> <output filename> <#cores task 1> .... <#cores task N>"
    exit(0)
    



# Parse the command line
groups = int(argv[1])
rows = int(argv[2])
columns = int(argv[3])
nodes_per_router = int(argv[4])
cores_per_node = int(argv[5])
dist = argv[6]
p = float(argv[7])
binout = open(argv[8], "wb")

# Compute the system size
router_count = groups * rows *columns
node_count = router_count * nodes_per_router
cores_per_router = nodes_per_router * cores_per_node
core_count = router_count * nodes_per_router * cores_per_node
task_sizes = [int(arg) for arg in argv[9:]]

# Create a list of tasks
tasks = range(0,len(task_sizes))

# Shuffle the tasks to give everyone the opportunity to have an "empty" machine
np.random.shuffle(tasks)

# Adjust the order of sizes
task_sizes = [task_sizes[i-1] for i in tasks ]

# Create random array of centers
task_centers = np.random.random_integers(0,router_count-1,len(tasks))

# Create the corresponding distributions
if dist == "Binomial":
    task_distributions = [Binomial(router_count,c,p) for c in task_centers]
elif dist == "Geometric":
    task_distributions = [Geometric(router_count,c,p) for c in task_centers]
   

# Slots
cores = np.zeros(core_count)

# List of empty router slots
empty = list(xrange(0, router_count))

# List of empty nodes
empty_nodes = list(xrange(0,node_count))

# Create scale down the task_sizes to leave some stragglers
task_sizes_tight = list(task_sizes)
for i,t in enumerate(task_sizes_tight):
    # How many routers would this job fill
    nr_rounters = t / cores_per_router
    if nr_rounters * cores_per_router < t:
        nr_rounters += 1
        
    # Pick no more than about 3% of the routers to be left out
    task_sizes_tight[i] = (97*nr_rounters) /  100 * cores_per_router
    

#print task_sizes
#print task_sizes_tight

# For all tasks
for t,size,dist in zip(tasks,task_sizes_tight,task_distributions):
    print "Started task ", i, size
    count = 0
    while count < size:
        
        # Choose a random node
        elem = random.choice(empty)
        
        # Get a uniform random number 
        test = np.random.uniform()
             
        # Get the current pmf value for the distribution
        current = dist.adjustedPMF(elem)

        if current < 0:
            print "Current ", current, " of ", elem, "  tested against ", test
            print dist.pmf(elem), dist.fill_sum
            exit(0)
        # If we pass the test
        if test < current:
            #print "Picked node", elem, " ", (size-count)/cores_per_node, " left to pick"
            #print "Current ", current, dist.pmf(elem)," of ", elem, "  tested against ", test
            
            # Now fill up all the cores as long as 
            # we have tasks
            i = 0
            while i<cores_per_node*nodes_per_router and count<size:
                cores[elem*cores_per_node*nodes_per_router + i] = t+1
                
                i += 1
                count += 1
             
            # Remove the router from the empty list
            empty.remove(elem)
            
            # Remove the corresponding nodes (This assumine the sizes for this 
            # loop are multiples of the core_per_router
            for i in xrange(0,nodes_per_router):
                empty_nodes.remove(elem*nodes_per_router + i)    
            
            # Adjust all distributions to include another filled element
            for d in task_distributions:
                d.fillSlot(elem)
                




# Now place the remaining cores of the tasks by uniformly picking
# empty nodes
for t,full,tight in zip(tasks,task_sizes,task_sizes_tight):
    size = full - tight
    
    count = 0
    while count < size:
        
        # Choose a random node
        elem = random.choice(empty_nodes)

        i = 0
        while i<cores_per_node and count<size:
            cores[elem*cores_per_node + i] = t+1
                
            i += 1
            count += 1
             
            # Remove the router from the empty list
        empty_nodes.remove(elem)
        



if False:
    pmfs = []
    scale = 0
    for d in task_distributions:
        pmfs.append([d.pmf(i) for i in xrange(0,router_count)])
        scale = max(scale,max(pmfs[-1]))
    
    import matplotlib.pyplot as plt
    
    
    fig, ax = plt.subplots()
    for pmf,t in zip(pmfs,tasks):
        #print "Colors ", colors[t]
        ax.plot(xrange(0,cores_per_node*nodes_per_router*router_count,cores_per_node),pmf,colors[t])
    
    #print ""
    for t in tasks:
        #print "Colors ", symbol[t]
        x = np.where(cores == t+1)
        ax.plot(x,[(t+1)*scale/len(tasks) ]*len(x),symbol[t])
        #print x
    
    plt.show()

print "g,r,c,n,core,jobid"
for t in xrange(0,len(tasks)):
     x = np.where(cores == t+1)
     
     # Now find the size of the t's job
     i = 0 
     while tasks[i] != t:
         i += 1
     
     
     if x[0].shape[0] != task_sizes[i]:
         print "Task assignment inconsistent for task ", t, ": found ", x[0].shape[0], " assigned cores but needed ", task_sizes[i]
         exit(0)
     #print x
     for rank in x[0]:
        dims = rank_to_coords(rank, groups, rows, columns, nodes_per_router, cores_per_node)
        print "%d,%d,%d,%d,%d,%d" % (dims[0],dims[1],dims[2],dims[3],dims[4],t)
        binout.write(struct.pack('6i', dims[0], dims[1], dims[2], dims[3], dims[4], t))

binout.close()

