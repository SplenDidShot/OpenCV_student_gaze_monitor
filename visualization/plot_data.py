import time
import math
from collections import deque , defaultdict

import matplotlib.animation as animation
from matplotlib import pyplot as plt



class DataPlot:
    def __init__(self, max_entries = 20):
        self.axis_x = deque(maxlen=max_entries)
        self.axis_neu = deque(maxlen=max_entries)
        self.axis_hap = deque(maxlen=max_entries)
        self.axis_sad = deque(maxlen=max_entries)
        self.axis_sur = deque(maxlen=max_entries)
        self.axis_ang = deque(maxlen=max_entries)


        self.max_entries = max_entries

    def add(self, x, neu,hap, sad, sur, ang):
        self.axis_x.append(x)
        self.axis_neu.append(neu)
        self.axis_hap.append(hap)
        self.axis_sad.append(sad)
        self.axis_sur.append(sur)
        self.axis_ang.append(ang)


class RealtimePlot:
    def __init__(self, axes):
     
        self.axes = axes

        self.lineplot, = axes.plot([], [], color="black", marker="o")
        self.lineplot2, = axes.plot([], [], "yo-")
        self.lineplot3, = axes.plot([], [], "mo-")
        self.lineplot4, = axes.plot([], [], color="orange", marker="o")
        self.lineplot5, = axes.plot([], [], "ro-")
        self.lineplot.set_label('Neutral')
        self.lineplot2.set_label('Happy')
        self.lineplot3.set_label('Sad')
        self.lineplot4.set_label('Surprised')
        self.lineplot5.set_label('Anger')
        self.axes.legend()

    def plot(self, dataPlot):
        self.axes.legend()

        self.lineplot.set_data(dataPlot.axis_x, dataPlot.axis_neu)
        self.lineplot2.set_data(dataPlot.axis_x, dataPlot.axis_hap)
        self.lineplot3.set_data(dataPlot.axis_x, dataPlot.axis_sad)
        self.lineplot4.set_data(dataPlot.axis_x, dataPlot.axis_sur)
        self.lineplot5.set_data(dataPlot.axis_x, dataPlot.axis_ang)

        self.axes.set_xlim(min(dataPlot.axis_x), max(dataPlot.axis_x))
        ymin = min([min(dataPlot.axis_neu), min(dataPlot.axis_hap), min(dataPlot.axis_sad), min(dataPlot.axis_sur), min(dataPlot.axis_ang)])-5
        ymax = max([max(dataPlot.axis_neu), max(dataPlot.axis_hap), max(dataPlot.axis_sad), max(dataPlot.axis_sur), max(dataPlot.axis_ang)])+5
        self.axes.set_ylim(ymin,ymax)
        self.axes.relim();