#!/usr/bin/env python2
# -*- coding: utf-8 -*-
"""
Created on Fri Jun 25 14:20:22 2021

@author: bmcq55
"""

import numpy as np
import matplotlib.pyplot as plt 
from mpl_toolkits import mplot3d

#Put in path of your .dat file
filename='Crimson_0M0HzLO_14M85HzDSP_1MSPS_ChA.dat'

#Put in sample rate of your file
sample_rate=1e6

#reads .dat file
data=np.fromfile(filename, dtype = 'short')

I=[]
for k in range(0,500,1):
    sub_data=data[(2*k)]
    I.append(sub_data)
    
Q=[]
for n in range(0,500,1):
    sub_data2=data[(2*n-1)]
    Q.append(sub_data2)

#plotting amplitude vs. samples 
plt.figure()
plt.title("Amp vs. Sample Plot of {}".format(filename))
plt.xlabel("Sample")
plt.ylabel("Amplitude")
plt.plot(I, label='in-phase')
plt.plot(Q, label='quadrature')
plt.legend()
plt.show()

#converts sample axis to time axis
time=np.arange(0,len(Q)/sample_rate, 1/sample_rate)

#plotting amplitude vs. time
plt.figure()
plt.title("Amp vs. Time Plot of {}".format(filename))
plt.xlabel("Time(seconds)")
plt.ylabel("Amplitude")
plt.plot(time[0:500], I,label='in-phase')
plt.plot(time[0:500], Q,label='quadrature')
plt.legend()
plt.show()

#3d Plot time=x-axis, I=y-axis, Q=z-axis
fig=plt.figure()
ax=plt.axes(projection="3d")
ax.plot3D(time[0:500],I,Q, c='green')
ax.set_xlabel('Time(seconds)')
ax.set_ylabel('In-phase')
ax.set_zlabel('Quadrature')
ax.set_title("3D Time Plot of {}".format(filename))
plt.show()



