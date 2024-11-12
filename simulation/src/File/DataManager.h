#pragma once

#include <iostream>
#include <memory>
#include <vector>
#include <chrono>
#include "Particle.h"
#include "vec3.h"
#include "Simulation.h"

class Simulation;

class DataManager
{
public:
    DataManager(std::string path);
    ~DataManager(){}

    std::string inputPath;
    std::string inputFormat;

    //path to the folder where the simulation data is saved
    std::string outputPath;
    std::string outputFormat;

//read the Config.ini file
    bool loadConfig(const std::string& filename, Simulation* simulation);

//save data in AGF and gadget format
    void saveData(std::vector<std::shared_ptr<Particle>> particles, int timeStep, int numberTimesteps, int numberOfParticles, double deltaTime, double endTime, double currentTime);

//
    bool loadICs(std::vector<std::shared_ptr<Particle>>& particles, Simulation* sim);

    //Data Size per 1000 particles in bytes
    double AGF_MemorySize = 77824;
    double AGFC_MemorySize = 20480;
    double AGFE_MemorySize = 94208;
    double AGFH_MemorySize = 13000; // not implemented yet
    double Gadget_MemorySize = 32000;

private:
    //AGF header
    struct AGFHeader
    {
        int numParticles[3];
        double deltaTime;
        double endTime;
        double currentTime;
    };

    //gadget2 header
    struct gadget2Header
    {
        unsigned int npart[6];
        double massarr[6];
        double time;
        double redshift;
        int flag_sfr;
        int flag_feedback;
        unsigned int npartTotal[6];
        int flag_cooling;
        int num_files;
        double BoxSize;
        double Omega0;
        double OmegaLambda;
        double HubbleParam;
        int flag_stellarage;
        int flag_metals;
        unsigned int npartTotalHighWord[6];
        int flag_entropy_instead_u;
        char fill[60]; // zur Auffüllung auf 256 Bytes
    };
};