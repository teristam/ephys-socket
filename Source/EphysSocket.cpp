#ifdef _WIN32
#include <Windows.h>
#endif

#include "EphysSocket.h"
#include "EphysSocketEditor.h"

using namespace EphysSocketNode;

DataThread* EphysSocket::createDataThread(SourceNode *sn)
{
    return new EphysSocket(sn);
}


EphysSocket::EphysSocket(SourceNode* sn) : DataThread(sn),
    port(DEFAULT_PORT),
    num_channels(DEFAULT_NUM_CHANNELS),
    num_samp(DEFAULT_NUM_SAMPLES),
    data_offset(DEFAULT_DATA_OFFSET),
    data_scale(DEFAULT_DATA_SCALE),
    sample_rate(DEFAULT_SAMPLE_RATE)
{
    socket = new DatagramSocket();
    socket->bindToPort(port);
    connected = (socket->waitUntilReady(true, 500) == 1); // Try to automatically open, dont worry if it does not work
    sourceBuffers.add(new DataBuffer(num_channels, 10000)); // start with 2 channels and automatically resize
    recvbuf = (uint16_t *) malloc(num_channels * num_samp * 2);
    convbuf = (float *) malloc(num_channels * num_samp * 4);
}

GenericEditor* EphysSocket::createEditor(SourceNode* sn)
{   
    editor = new EphysSocketEditor(sn, this);
    return editor;

}



EphysSocket::~EphysSocket()
{
    free(recvbuf);
    free(convbuf);
}

void EphysSocket::resizeChanSamp()
{
    sourceBuffers[0]->resize(num_channels, 10000);
    recvbuf = (uint16_t *)realloc(recvbuf, num_channels * num_samp * 2);
    convbuf = (float *)realloc(convbuf, num_channels * num_samp * 4);


    timestamps.resize(num_samp);
    ttlEventWords.resize(num_samp);
    
}

int EphysSocket::getNumChannels() const
{
    return num_channels;
}

int EphysSocket::getNumDataOutputs(DataChannel::DataChannelTypes type, int subproc) const
{
    if (type == DataChannel::HEADSTAGE_CHANNEL)
        return num_channels;
    else
        return 0; 
}

int EphysSocket::getNumTTLOutputs(int subproc) const
{
    return 0; 
}

float EphysSocket::getSampleRate(int subproc) const
{
    return sample_rate;
}

float EphysSocket::getBitVolts (const DataChannel* ch) const
{
    return data_scale;
}

bool EphysSocket::foundInputSource()
{
    return connected;
}

bool EphysSocket::startAcquisition()
{
    resizeChanSamp();

    total_samples = 0;

    //startTimer(5000);

    startThread();

    return true;
}

void  EphysSocket::tryToConnect()
{
    socket->shutdown();
    socket = new DatagramSocket();
    bool bound = socket->bindToPort(port);
    int connection_status = -1;
    if (bound)
    {
        std::cout << "Socket bound to port " << port << std::endl;
        connection_status = socket->waitUntilReady(true, 500);
        //connected = (socket->waitUntilReady(true, 500) == 1);
        connected = (connection_status == 1);
    }
    else {
        std::cout << "Could not bind socket to port " << port << std::endl;
    }
    

    if (connected)
    {
        std::cout << "Socket connected." << std::endl;

    }
    else {
        std::cout << "Socket failed to connect: " << connection_status << std::endl;
    }

}

bool EphysSocket::stopAcquisition()
{
    if (isThreadRunning())
    {
        signalThreadShouldExit();
    }

    waitForThreadToExit(500);

    //stopTimer();

    sourceBuffers[0]->clear();
    return true;
}

bool EphysSocket::updateBuffer()
{



    //recvbuf is two bype
    int rc = socket->read(recvbuf, num_channels * num_samp * 2, true); //the data size of each datagram must match

    if (rc > 0) {
        std::cout << "Data:" << rc << std::endl;

        for (int i = 0; i < num_samp; i++) {
            std::cout << recvbuf[i] << " ";
        }
    }


    if (rc == -1)
    {
        CoreServices::sendStatusMessage("Ephys Socket: Data shape mismatch");
        std::cout << "Error in socket read" << std::endl;
        return false;
    }
   
    // Transpose because the chunkSize argument in addToBuffer does not seem to do anything
    if (transpose) {
        int k = 0;
        for (int i = 0; i < num_samp; i++) {
            for (int j = 0; j < num_channels; j++) {
                convbuf[k++] = data_scale *  (float)(recvbuf[j*num_samp + i] - data_offset); //convert the input data to proper values

            }
            timestamps.set(i, total_samples + i);
        }
    } else {
        for (int i = 0; i < num_samp * num_channels; i++)
        {
            convbuf[i] = data_scale * (float)(recvbuf[i] - data_offset);
            timestamps.set(i, total_samples + i);
        }
            
    }

    sourceBuffers[0]->addToBuffer(convbuf, 
                                  &timestamps.getReference(0), 
                                  &ttlEventWords.getReference(0), 
                                  num_samp);

    total_samples += num_samp;

    return true;
}



void EphysSocket::timerCallback()
{
    //std::cout << "Expected samples: " << int(sample_rate * 5) << ", Actual samples: " << total_samples << std::endl;
    
    relative_sample_rate = (sample_rate * 5) / float(total_samples);

    //total_samples = 0;
}
