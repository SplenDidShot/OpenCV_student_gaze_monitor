
# coding: utf-8

# In[109]:

import paho.mqtt.client as paho
import json
import matplotlib.pyplot as plt
from random import randint


from plot_data import DataPlot, RealtimePlot


fig, axes = plt.subplots()
axes.set_ylabel('Number of Students')
axes.set_title('Data from Student Gaze monitor')

data = DataPlot()
dataPlotting= RealtimePlot(axes)
count=0

def on_connect(mosq, obj, rc):
    print("connect rc: "+str(rc))
    mqttc.publish("mbed-sample","Python Script Test Message.");

def on_message(mosq, obj, msg):
    msg_str = msg.payload.decode('utf-8')
    print(msg_str)
    msg_dict = json.loads(msg_str)
    global count
    count += 1
    data.add(count, int(msg_dict['neutral']), int(msg_dict['happy']), int(msg_dict['sad']), int(msg_dict['surprised']), int(msg_dict['anger']))
    dataPlotting.plot(data)
    plt.pause(0.001)
    print( "Received on topic: " + msg.topic + " Message: "+str(msg.payload) + "\n");

def on_subscribe(mosq, obj, mid, granted_qos):
    print("Subscribed OK")


mqttc = paho.Client()

# Settings for connection
host = "test.mosquitto.org"
topic= "retail/traffic"
port = 1883

# Set callbacks
mqttc.on_message = on_message
mqttc.on_connect = on_connect
mqttc.on_subscribe = on_subscribe



# Connect and subscribe
print("Connecting to " +host +"/" +topic)
mqttc.connect(host, port, 60)
mqttc.subscribe(topic, 0)

# Wait forever, receiving messages
rc = 0
while rc == 0:
    rc = mqttc.loop()

print("rc: "+str(rc))


# Connect and subscribe
print("Connecting to " +host +"/" +topic)
mqttc.connect(host, port, 60)
mqttc.subscribe(topic, 0)

# Wait forever, receiving messages
rc = 0
while rc == 0:
    rc = mqttc.loop()

print("rc: "+str(rc))

