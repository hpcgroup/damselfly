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
from __builtin__ import True

symbol = ["ro","g^","bs","yo"]
colors = ["r","g","b","y"]

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
        

if len(argv) < 7:
    print "Usage: %s <numGroups> <numRows> <numColumns> <numNodesPerRouter> <numCoresPerNode>  [Binomial|Geometric] <p> <#nodes task 1> .... <#nodes task N>"
    exit(0)
    



# Parse the command line
groups = int(argv[1])
rows = int(argv[2])
columns = int(argv[3])
nodes_per_router = int(argv[4])
cores_per_node = int(argv[5])
dist = argv[6]
p = float(argv[7])

# Compute the system size
node_count = groups*rows*columns*nodes_per_router
core_count = node_count*cores_per_node
task_sizes = [int(arg) for arg in argv[8:]]

# Create a list of tasks
tasks = range(0,len(task_sizes))

# Shuffle the tasks to give everyone the opportunity to have an "empty" machine
np.random.shuffle(tasks)

# Adjust the order of sizes
task_sizes = [task_sizes[i-1] for i in tasks ]

# Create random array of centers
task_centers = np.random.random_integers(0,node_count-1,len(tasks))

# Create the corresponding distributions
if dist == "Binomial":
    task_distributions = [Binomial(node_count,c,p) for c in task_centers]
elif dist == "Geometric":
    task_distributions = [Geometric(node_count,c,p) for c in task_centers]
   

# Slots
cores = np.zeros(core_count)

# List of empty nodes slots
empty = list(xrange(0,node_count))

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
            while i<cores_per_node and count<size:
                cores[elem*cores_per_node + i] = t+1
                i += 1
                count += 1
             
            # Remove the node from the empty list
            empty.remove(elem)
            
            # Adjust all distributions to include another filled element
            for d in task_distributions:
                d.fillSlot(elem)
                


print "Starting plotting"
pmfs = []
scale = 0
for d in task_distributions:
    pmfs.append([d.pmf(i) for i in xrange(0,node_count)])
    scale = max(scale,max(pmfs[-1]))

import matplotlib.pyplot as plt


fig, ax = plt.subplots()
for pmf,t in zip(pmfs,tasks):
    #print "Colors ", colors[t]
    print len(pmf), node_count
    ax.plot(xrange(0,cores_per_node*node_count,cores_per_node),pmf,colors[t])

#print ""
for t in tasks:
    #print "Colors ", symbol[t]
    x = np.where(cores == t+1)
    ax.plot(x,[(t+1)*scale/len(tasks) ]*len(x),symbol[t])
    #print x

plt.show()




