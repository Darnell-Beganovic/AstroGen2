#include "DataManager.h"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <chrono>
#include <cstring>
#include "Particle.h"
#include "vec3.h"
#include "Constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <locale>
#include "Units.h"
#include <unistd.h>
#include <cstdint>
#include "Tree/Tree.h"
#include <HighFive/HighFive.hpp>
#include "Log.h"

using namespace std;
namespace fs = std::filesystem;

DataManager::DataManager(std::string path)
{
  this->outputPath = path;

  
}

bool DataManager::setupFile()
{
    // Sicherstellen, dass der Pfad existiert
    if (!fs::exists(this->outputPath))
    {
        fs::create_directories(this->outputPath);
    }

    ending = "";
    if(outputFormat == "ag") ending = ".ag";
    else if(outputFormat == "agc") ending = ".agc";
    else if(outputFormat == "age") ending = ".age";
    else if(outputFormat == "h5") ending = ".h5";
    else if(outputFormat == "gadget") ending = ".gadget";
    else
    {
        std::cerr << "Unknown output data format: " << outputFormat << std::endl;
        return false;
    }

    return true;
}



// HDF5  hdf5  h5
void DataManager::saveData(std::shared_ptr<Tree> tree, int timeStep, int numberTimesteps, int numberOfParticles, double deltaTime, double endTime, double currentTime)
{
    // Überprüfe Ordner und Dateiendung
    if(!setupFile()) return;
    std::string filename = this->outputPath + std::to_string(timeStep) + ending;
    std::cerr << filename << std::endl;

    try {
        HighFive::File file(filename, HighFive::File::Overwrite);

        // -------- Header ---------
        std::vector<int> numParticles(3, 0);
        std::vector<ParticleData> particles;
        std::vector<NodeData> nodes;
        particles.reserve(numberOfParticles);
        nodes.reserve(numberOfParticles); // Annahme: maximal so viele Knoten wie Partikel

        uint32_t currentNodeID = 1; // Start bei 1, 0 könnte Root sein

        // Sammle alle Partikeldaten und die Baumstruktur
        collectParticleData(tree->root, particles, nodes, 0, currentNodeID); // Root hat keine Eltern

        // Zähle die Partikeltypen
        for (const auto& particle : particles) {
            if (particle.type == 1)
                numParticles[0]++;
            if (particle.type == 2)
                numParticles[1]++;
            if (particle.type == 3)
                numParticles[2]++;
        }

        HighFive::Group headerGroup = file.createGroup("header");

        // Schreiben des Datasets "numParticles"
        headerGroup.createDataSet<int>("numParticles", HighFive::DataSpace::From(numParticles)).write(numParticles);

        // Schreiben weiterer Datasets
        headerGroup.createDataSet<double>("deltaTime", HighFive::DataSpace::From(deltaTime)).write(deltaTime);
        headerGroup.createDataSet<double>("endTime", HighFive::DataSpace::From(endTime)).write(endTime);
        headerGroup.createDataSet<double>("currentTime", HighFive::DataSpace::From(currentTime)).write(currentTime);

        // ---------- Partikel Datasets -----------
        HighFive::Group particlesGroup = file.createGroup("particles");

        // Vorbereitung der Datenstrukturen für Bulk-Write
        std::vector<std::array<double, 3>> positions;
        std::vector<std::array<double, 3>> velocities;
        std::vector<std::array<double, 3>> accelerations;
        std::vector<double> masses;
        std::vector<double> Ts;
        std::vector<double> visualDensities;
        std::vector<uint8_t> types;
        std::vector<uint8_t> galaxyParts;
        std::vector<uint32_t> ids;

        positions.reserve(particles.size());
        velocities.reserve(particles.size());
        accelerations.reserve(particles.size());
        masses.reserve(particles.size());
        Ts.reserve(particles.size());
        visualDensities.reserve(particles.size());
        types.reserve(particles.size());
        galaxyParts.reserve(particles.size());
        ids.reserve(particles.size());

        for (const auto& p : particles) {
            positions.push_back(p.position);
            velocities.push_back(p.velocity);
            accelerations.push_back(p.acceleration);
            masses.push_back(p.mass);
            Ts.push_back(p.T);
            visualDensities.push_back(p.visualDensity);
            types.push_back(p.type);
            galaxyParts.push_back(p.galaxyPart);
            ids.push_back(p.id);
        }

        // Schreibe die Datasets einmal
        particlesGroup.createDataSet<double>("position", HighFive::DataSpace::From(positions)).write(positions);
        particlesGroup.createDataSet<double>("velocity", HighFive::DataSpace::From(velocities)).write(velocities);
        particlesGroup.createDataSet<double>("acceleration", HighFive::DataSpace::From(accelerations)).write(accelerations);
        particlesGroup.createDataSet<double>("mass", HighFive::DataSpace::From(masses)).write(masses);
        particlesGroup.createDataSet<double>("T", HighFive::DataSpace::From(Ts)).write(Ts);
        particlesGroup.createDataSet<double>("visualDensity", HighFive::DataSpace::From(visualDensities)).write(visualDensities);
        particlesGroup.createDataSet<uint8_t>("type", HighFive::DataSpace::From(types)).write(types);
        particlesGroup.createDataSet<uint8_t>("galaxyPart", HighFive::DataSpace::From(galaxyParts)).write(galaxyParts);
        particlesGroup.createDataSet<uint32_t>("id", HighFive::DataSpace::From(ids)).write(ids);

        // ---------- Baumstruktur Datasets -----------
        HighFive::Group treeGroup = file.createGroup("tree");

        // Vorbereitung der Datenstrukturen für die Baumstruktur
        std::vector<uint32_t> nodeIDs;
        std::vector<uint32_t> parentIDs;
        std::vector<std::vector<uint32_t>> childrenIDs; // Variable Länge, daher benötigen wir einen anderen Ansatz

        nodeIDs.reserve(nodes.size());
        parentIDs.reserve(nodes.size());

        // Um die Kinder in einem einzigen Array zu speichern, können wir auch eine Offset-Liste verwenden
        std::vector<uint32_t> allChildrenIDs;
        std::vector<uint32_t> childrenOffsets; // Startindex jedes Knotens' Kinder in allChildrenIDs
        childrenOffsets.reserve(nodes.size() + 1);
        childrenOffsets.push_back(0); // Start bei 0

        for (const auto& node : nodes) {
            nodeIDs.push_back(node.nodeID);
            parentIDs.push_back(node.parentID);
            allChildrenIDs.insert(allChildrenIDs.end(), node.childrenIDs.begin(), node.childrenIDs.end());
            childrenOffsets.push_back(static_cast<uint32_t>(allChildrenIDs.size()));
        }

        // Schreibe die Baumstruktur-Datasets
        treeGroup.createDataSet<uint32_t>("nodeID", HighFive::DataSpace::From(nodeIDs)).write(nodeIDs);
        treeGroup.createDataSet<uint32_t>("parentID", HighFive::DataSpace::From(parentIDs)).write(parentIDs);
        treeGroup.createDataSet<uint32_t>("childrenIDs", HighFive::DataSpace::From(allChildrenIDs)).write(allChildrenIDs);
        treeGroup.createDataSet<uint32_t>("childrenOffsets", HighFive::DataSpace::From(childrenOffsets)).write(childrenOffsets);

        std::cout << "HDF5-Datei '" << filename << "' erfolgreich erstellt und Daten geschrieben!" << std::endl;
    }
    catch (const HighFive::Exception& err) {
        std::cerr << "HighFive-Fehler: " << err.what() << std::endl;
    }
    catch (const std::exception& ex) {
        std::cerr << "Allgemeiner Fehler: " << ex.what() << std::endl;
    }
}


void DataManager::collectParticleData(const std::shared_ptr<Node>& node, std::vector<ParticleData>& particles, std::vector<NodeData>& nodes, uint32_t parentID, uint32_t& currentNodeID)
{
    uint32_t nodeID = currentNodeID++;
    NodeData nodeData;
    nodeData.nodeID = nodeID;
    nodeData.parentID = parentID;
    nodeData.isLeaf = node->isLeaf;

    if (node->isLeaf && node->particle) {
        nodeData.particleIndex = static_cast<uint32_t>(particles.size());
        ParticleData pdata;
        pdata.position = {node->particle->position.x, node->particle->position.y, node->particle->position.z};
        pdata.velocity = {node->particle->velocity.x, node->particle->velocity.y, node->particle->velocity.z};
        pdata.acceleration = {node->particle->acceleration.x, node->particle->acceleration.y, node->particle->acceleration.z};
        pdata.mass = node->particle->mass;
        pdata.T = node->particle->T;
        pdata.visualDensity = node->particle->visualDensity;
        pdata.type = node->particle->type;
        pdata.galaxyPart = node->particle->galaxyPart;
        pdata.id = node->particle->id;
        particles.push_back(pdata);
    }

    // Sammle Kinder
    for (size_t i = 0; i < 8; ++i) {
        if (node->children[i]) {
            uint32_t childID = currentNodeID;
            nodeData.childrenIDs.push_back(childID);
            collectParticleData(node->children[i], particles, nodes, nodeID, currentNodeID);
        }
    }

    nodes.push_back(nodeData);
}



void DataManager::saveData(std::vector<std::shared_ptr<Particle>> particles, int timeStep, int numberTimesteps, int numberOfParticles, double deltaTime, double endTime, double currentTime)
{
    // check for folder and file ending
    if(!setupFile()) return;

    // Dateiname basierend auf dem Zeitschritt
    std::string filename = this->outputPath + std::to_string(timeStep) + ending;
    std::ofstream file(filename, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error opening datafile: " << filename << std::endl;
        return;
    }

    if (outputFormat == "ag")
    {

        //write the header
        AGFHeader header;
        //find the number of particles per type
        int numParticles[3] = {0, 0, 0};
        for (int i = 0; i < numberOfParticles; i++)
        {
            if(particles[i]->type == 1) numParticles[0]++;
            if(particles[i]->type == 2) numParticles[1]++;
            if(particles[i]->type == 3) numParticles[2]++;
        }
        header.numParticles[0] = numParticles[0];
        header.numParticles[1] = numParticles[1];
        header.numParticles[2] = numParticles[2];
        header.deltaTime = deltaTime;
        header.endTime = endTime;
        header.currentTime = currentTime;

        file.write(reinterpret_cast<char*>(&header), sizeof(header));

        ag_MemorySize = particles.size() * (sizeof(vec3) * 2 + sizeof(double) * 3 + sizeof(uint8_t) * 2 + sizeof(uint32_t));
        size_t totalSize = ag_MemorySize;
        
        // Speicher für den Puffer allokieren
        char* buffer = reinterpret_cast<char*>(malloc(totalSize));
        if (buffer) {
            char* ptr = buffer;
            for (const auto& particle : particles) {
                memcpy(ptr, &particle->position, sizeof(vec3)); ptr += sizeof(vec3);
                memcpy(ptr, &particle->velocity, sizeof(vec3)); ptr += sizeof(vec3);
                memcpy(ptr, &particle->mass, sizeof(double)); ptr += sizeof(double);
                memcpy(ptr, &particle->T, sizeof(double)); ptr += sizeof(double);
                memcpy(ptr, &particle->visualDensity, sizeof(double)); ptr += sizeof(double); // Added visualDensity not real SPH density
                memcpy(ptr, &particle->type, sizeof(uint8_t)); ptr += sizeof(uint8_t);
                memcpy(ptr, &particle->galaxyPart, sizeof(uint8_t)); ptr += sizeof(uint8_t);
                memcpy(ptr, &particle->id, sizeof(uint32_t)); ptr += sizeof(uint32_t);

            }
            file.write(buffer, totalSize);
            free(buffer);
        }
    }
    else if (outputFormat == "agc")
    {

        //write the header
        AGFHeader header;
        //find the number of particles per type
        int numParticles[3] = {0, 0, 0};
        for (int i = 0; i < numberOfParticles; i++)
        {
            if(particles[i]->type == 1) numParticles[0]++;
            if(particles[i]->type == 2) numParticles[1]++;
            if(particles[i]->type == 3) numParticles[2]++;
        }
        header.numParticles[0] = numParticles[0];
        header.numParticles[1] = numParticles[1];
        header.numParticles[2] = numParticles[2];
        header.deltaTime = deltaTime;
        header.endTime = endTime;
        header.currentTime = currentTime;

        file.write(reinterpret_cast<char*>(&header), sizeof(header));
        agc_MemorySize = (sizeof(float) * 3 + sizeof(float) + sizeof(uint8_t)) * particles.size();
        size_t totalSize = agc_MemorySize;

        // Puffer allokieren
        std::vector<char> buffer(totalSize);
        char* ptr = buffer.data();

        for (const auto& particle : particles)
        {
            // Position als float konvertieren
            float posX = static_cast<float>(particle->position.x);
            float posY = static_cast<float>(particle->position.y);
            float posZ = static_cast<float>(particle->position.z);
            memcpy(ptr, &posX, sizeof(float)); ptr += sizeof(float);
            memcpy(ptr, &posY, sizeof(float)); ptr += sizeof(float);
            memcpy(ptr, &posZ, sizeof(float)); ptr += sizeof(float);
            
            float visualDensity = static_cast<float>(particle->visualDensity);
            memcpy(ptr, &visualDensity, sizeof(float)); ptr += sizeof(float);


            // type als int speichern
            uint8_t type = particle->type;
            memcpy(ptr, &type, sizeof(uint8_t)); ptr += sizeof(uint8_t);
        }

        // Puffer in die Datei schreiben
        file.write(buffer.data(), totalSize);
    }
    else if (outputFormat == "age")
    {

        //write the header
        AGFHeader header;
        //find the number of particles per type
        int numParticles[3] = {0, 0, 0};
        for (int i = 0; i < numberOfParticles; i++)
        {
            if(particles[i]->type == 1) numParticles[0]++;
            if(particles[i]->type == 2) numParticles[1]++;
            if(particles[i]->type == 3) numParticles[2]++;
        }
        header.numParticles[0] = numParticles[0];
        header.numParticles[1] = numParticles[1];
        header.numParticles[2] = numParticles[2];
        header.deltaTime = deltaTime;
        header.endTime = endTime;
        header.currentTime = currentTime;

        file.write(reinterpret_cast<char*>(&header), sizeof(header));
        age_MemorySize =  particles.size() * (sizeof(vec3) * 2 + sizeof(double) * 5 + sizeof(uint8_t) * 2 + sizeof(uint32_t));
        size_t totalSize = age_MemorySize;
        
        // Speicher für den Puffer allokieren
        char* buffer = reinterpret_cast<char*>(malloc(totalSize));
        if (buffer) {
            char* ptr = buffer;
            for (const auto& particle : particles) {
                memcpy(ptr, &particle->position, sizeof(vec3)); ptr += sizeof(vec3);
                memcpy(ptr, &particle->velocity, sizeof(vec3)); ptr += sizeof(vec3);
                memcpy(ptr, &particle->mass, sizeof(double)); ptr += sizeof(double);
                memcpy(ptr, &particle->T, sizeof(double)); ptr += sizeof(double);
                memcpy(ptr, &particle->P, sizeof(double)); ptr += sizeof(double);
                memcpy(ptr, &particle->visualDensity, sizeof(double)); ptr += sizeof(double); // Added visualDensity not real SPH density
                memcpy(ptr, &particle->U, sizeof(double)); ptr += sizeof(double);
                memcpy(ptr, &particle->type, sizeof(uint8_t)); ptr += sizeof(uint8_t);
                memcpy(ptr, &particle->galaxyPart, sizeof(uint8_t)); ptr += sizeof(uint8_t);
                memcpy(ptr, &particle->id, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            }
            file.write(buffer, totalSize);
            free(buffer);
        }
    }
    else if (outputFormat == "gadget")
    {

        // Initialize the header
        gadget2Header header;
        memset(&header, 0, sizeof(header)); // Zero-initialize the header

        // Listen zur Organisation der Partikel nach Gadget-Typ
        std::vector<Particle*> gas_particles;
        std::vector<Particle*> halo_particles;
        std::vector<Particle*> disk_particles;
        std::vector<Particle*> bulge_particles;

        gadget_MemorySize = sizeof(header) + particles.size() * (6 * sizeof(float) + sizeof(unsigned int) + sizeof(float)) +  gas_particles.size() * sizeof(float);

        // Map our particle types to Gadget2 types and count particles per type
        for (int i = 0; i < numberOfParticles; i++) {
            int gadget_type = 0;
            
            // Map AstroGen2 particle types to Gadget2 types
            if (particles[i]->galaxyPart == 1 && particles[i]->type == 1)
            {
                //save disk as a disk particle
                gadget_type = 2;
                gas_particles.push_back(particles[i].get());
            } 
            if (particles[i]->galaxyPart == 3 && particles[i]->type == 3) 
            {
                //save halo as a halo particle
                gadget_type = 1;
                disk_particles.push_back(particles[i].get());
            } 
            if (particles[i]->galaxyPart == 2 && particles[i]->type == 1)
            {
                //save bulge as a bulge particle
                gadget_type = 3;
                halo_particles.push_back(particles[i].get());
            }

            //exeption: if if particle is a gas no matter if it is a disk or bulge particle save it as a gas particle
            if (particles[i]->type == 2) 
            {
                gadget_type = 0;
                gas_particles.push_back(particles[i].get());
            }

            header.npart[gadget_type]++;
            header.npartTotal[gadget_type]++;
        }

        // Set massarr to 0 to store individual masses
        for (int i = 0; i < 6; i++) {
            header.massarr[i] = 0.0;
        }

        // Set other header parameters
        header.time = currentTime;
        header.redshift = 0.0;
        header.num_files = 1;
        header.BoxSize = 0.0; // Set as appropriate for your simulation
        header.Omega0 = 0.0;
        header.OmegaLambda = 0.0;
        header.HubbleParam = 1.0;

        // Write the header with block sizes
        unsigned int block_size = sizeof(header);
        file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));
        file.write(reinterpret_cast<char*>(&header), sizeof(header));
        file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));

        // Funktion zur Datenvorbereitung (Position, Geschwindigkeit)
        auto prepareData = [](const std::vector<Particle*>& particles, std::vector<float>& data, float unit) {
            data.resize(particles.size() * 3);
            for (size_t i = 0; i < particles.size(); i++) {
                data[3*i]     = static_cast<float>(particles[i]->position.x / unit);
                data[3*i + 1] = static_cast<float>(particles[i]->position.y / unit);
                data[3*i + 2] = static_cast<float>(particles[i]->position.z / unit);
            }
        };

        // Positionen in der Reihenfolge Gas, Halo, Disk, Bulge vorbereiten und schreiben
        std::vector<float> positions;
        prepareData(gas_particles, positions, Units::KPC);
        std::vector<float> halo_positions;
        prepareData(halo_particles, halo_positions, Units::KPC);
        std::vector<float> disk_positions;
        prepareData(disk_particles, disk_positions, Units::KPC);
        std::vector<float> bulge_positions;
        prepareData(bulge_particles, bulge_positions, Units::KPC);

        positions.insert(positions.end(), halo_positions.begin(), halo_positions.end());
        positions.insert(positions.end(), disk_positions.begin(), disk_positions.end());
        positions.insert(positions.end(), bulge_positions.begin(), bulge_positions.end());

        block_size = positions.size() * sizeof(float);
        file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));
        file.write(reinterpret_cast<char*>(positions.data()), block_size);
        file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));

        // Geschwindigkeiten in der Reihenfolge Gas, Halo, Disk, Bulge vorbereiten und schreiben
        std::vector<float> velocities;
        prepareData(gas_particles, velocities, Units::KMS);
        std::vector<float> halo_velocities;
        prepareData(halo_particles, halo_velocities, Units::KMS);
        std::vector<float> disk_velocities;
        prepareData(disk_particles, disk_velocities, Units::KMS);
        std::vector<float> bulge_velocities;
        prepareData(bulge_particles, bulge_velocities, Units::KMS);

        velocities.insert(velocities.end(), halo_velocities.begin(), halo_velocities.end());
        velocities.insert(velocities.end(), disk_velocities.begin(), disk_velocities.end());
        velocities.insert(velocities.end(), bulge_velocities.begin(), bulge_velocities.end());

        block_size = velocities.size() * sizeof(float);
        file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));
        file.write(reinterpret_cast<char*>(velocities.data()), block_size);
        file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));

        // IDs in der Reihenfolge Gas, Halo, Disk, Bulge vorbereiten und schreiben
        std::vector<unsigned int> ids;
        for (const auto& particle : gas_particles) ids.push_back(particle->id);
        for (const auto& particle : halo_particles) ids.push_back(particle->id);
        for (const auto& particle : disk_particles) ids.push_back(particle->id);
        for (const auto& particle : bulge_particles) ids.push_back(particle->id);

        block_size = ids.size() * sizeof(unsigned int);
        file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));
        file.write(reinterpret_cast<char*>(ids.data()), block_size);
        file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));

        // Massen in der Reihenfolge Gas, Halo, Disk, Bulge vorbereiten und schreiben
        std::vector<float> masses;
        for (const auto& particle : gas_particles) masses.push_back(static_cast<float>(particle->mass / (Units::MSUN * 1e10)));
        for (const auto& particle : halo_particles) masses.push_back(static_cast<float>(particle->mass / (Units::MSUN * 1e10)));
        for (const auto& particle : disk_particles) masses.push_back(static_cast<float>(particle->mass / (Units::MSUN * 1e10)));
        for (const auto& particle : bulge_particles) masses.push_back(static_cast<float>(particle->mass / (Units::MSUN * 1e10)));

        block_size = masses.size() * sizeof(float);
        file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));
        file.write(reinterpret_cast<char*>(masses.data()), block_size);
        file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));

        // U-Werte (interne Energie) nur für Gaspartikel schreiben
        if (!gas_particles.empty()) {
            std::vector<float> u_values;
            for (const auto& particle : gas_particles) {
                u_values.push_back(static_cast<float>(particle->U / 1e6)); // U in Code-Einheiten konvertieren
            }
            block_size = u_values.size() * sizeof(float);
            file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));
            file.write(reinterpret_cast<char*>(u_values.data()), block_size);
            file.write(reinterpret_cast<char*>(&block_size), sizeof(block_size));
        }

        file.close();
    }
    else
    {
        std::cerr << "Unknown output data format: " << outputFormat << std::endl;
    }

    file.close();
}

bool DataManager::loadICs(std::vector<std::shared_ptr<Particle>>& particles, Simulation* sim)
{
    // Öffne die Datei im Binärmodus
    std::ifstream file("../../input_data/" + inputPath, std::ios::binary);
    
    if (!file) {
        std::cerr << "Fehler: Konnte die Datei nicht öffnen: " << inputPath << std::endl;
        return false;
    }

    if(inputFormat == "ag")
    {
        std::cout << "reading ag initial condition data ..." << std::endl;
        // Header auslesen
        AGFHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!file) {
            std::cerr << "Fehler: Konnte den ag-Header nicht lesen!" << std::endl;
            return false;
        }

        // Anzahl der Partikel berechnen
        unsigned int total_particles = header.numParticles[0] + header.numParticles[1] + header.numParticles[2];

        sim->numberOfParticles = total_particles;
        
        // Speicherplatz für Partikel reservieren
        particles.reserve(total_particles);

        // Partikel auslesen
        for (unsigned int i = 0; i < total_particles; ++i)
        {
            Particle particle;
            file.read(reinterpret_cast<char*>(&particle.position), sizeof(vec3));
            file.read(reinterpret_cast<char*>(&particle.velocity), sizeof(vec3));
            file.read(reinterpret_cast<char*>(&particle.mass), sizeof(double));
            file.read(reinterpret_cast<char*>(&particle.T), sizeof(double));
            file.read(reinterpret_cast<char*>(&particle.visualDensity), sizeof(double));
            file.read(reinterpret_cast<char*>(&particle.type), sizeof(uint8_t));
            file.read(reinterpret_cast<char*>(&particle.galaxyPart), sizeof(uint8_t));
            file.read(reinterpret_cast<char*>(&particle.id), sizeof(uint32_t));
            particles.push_back(std::make_shared<Particle>(particle));
        }

        file.close();
        std::cout << "successfully read ag initial condition data" << std::endl;
        return true;
    }
    else if (inputFormat == "agc")
    {
        std::cout << "reading agc initial condition data ..." << std::endl;
        // Header auslesen
        AGFHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!file) {
            std::cerr << "Fehler: Konnte den agc-Header nicht lesen!" << std::endl;
            return false;
        }

        // Anzahl der Partikel berechnen
        unsigned int total_particles = header.numParticles[0] + header.numParticles[1] + header.numParticles[2];

        sim->numberOfParticles = total_particles;
        
        // Speicherplatz für Partikel reservieren
        particles.reserve(total_particles);

        // Partikel auslesen
        for (unsigned int i = 0; i < total_particles; ++i)
        {
            Particle particle;
            float posX, posY, posZ;
            file.read(reinterpret_cast<char*>(&posX), sizeof(float));
            file.read(reinterpret_cast<char*>(&posY), sizeof(float));
            file.read(reinterpret_cast<char*>(&posZ), sizeof(float));
            particle.position = vec3(posX, posY, posZ);
            float visualDensity;
            file.read(reinterpret_cast<char*>(&visualDensity), sizeof(float));
            particle.visualDensity = visualDensity;
            uint8_t type;
            file.read(reinterpret_cast<char*>(&type), sizeof(uint8_t));
            particle.type = type;
            particles.push_back(std::make_shared<Particle>(particle));
        }

        file.close();
        std::cout << "successfully read agc initial condition data" << std::endl;
        return true;
    }
    else if (inputFormat == "age")
    {
        std::cout << "reading age initial condition data ..." << std::endl;
        // Header auslesen
        AGFHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!file) {
            std::cerr << "Fehler: Konnte den age-Header nicht lesen!" << std::endl;
            return false;
        }

        // Anzahl der Partikel berechnen
        unsigned int total_particles = header.numParticles[0] + header.numParticles[1] + header.numParticles[2];

        sim->numberOfParticles = total_particles;
        
        // Speicherplatz für Partikel reservieren
        particles.reserve(total_particles);

        // Partikel auslesen
        for (unsigned int i = 0; i < total_particles; ++i)
        {
            Particle particle;
            file.read(reinterpret_cast<char*>(&particle.position), sizeof(vec3));
            file.read(reinterpret_cast<char*>(&particle.velocity), sizeof(vec3));
            file.read(reinterpret_cast<char*>(&particle.mass), sizeof(double));
            file.read(reinterpret_cast<char*>(&particle.T), sizeof(double));
            file.read(reinterpret_cast<char*>(&particle.P), sizeof(double));
            file.read(reinterpret_cast<char*>(&particle.visualDensity), sizeof(double));
            file.read(reinterpret_cast<char*>(&particle.U), sizeof(double));
            file.read(reinterpret_cast<char*>(&particle.type), sizeof(uint8_t));
            file.read(reinterpret_cast<char*>(&particle.galaxyPart), sizeof(uint8_t));
            file.read(reinterpret_cast<char*>(&particle.id), sizeof(uint32_t));
            particles.push_back(std::make_shared<Particle>(particle));
        }

        file.close();

        std::cout << "successfully read age initial condition data" << std::endl;
        return true;
    }
    else if(inputFormat == "hdf5")
    {
        //...
    }
    else if(inputFormat == "gadget")
    {
        std::cout << "reading gadget2 snapshot ..." << std::endl;
        // Gadget2 Header auslesen
        gadget2Header header;

        // Lesen der ersten Blockgröße vor dem Header
        unsigned int block_size_start;
        file.read(reinterpret_cast<char*>(&block_size_start), sizeof(block_size_start));
        if (!file) {
            std::cerr << "Fehler: Konnte die Start-Blockgröße nicht lesen!" << std::endl;
            return false;
        }

        // Lesen des Headers
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!file) {
            std::cerr << "Fehler: Konnte den Header aus der Datei nicht lesen!" << std::endl;
            return false;
        }

        // Lesen der Blockgröße nach dem Header
        unsigned int block_size_end;
        file.read(reinterpret_cast<char*>(&block_size_end), sizeof(block_size_end));
        if (!file) {
            std::cerr << "Fehler: Konnte die End-Blockgröße des Headers nicht lesen!" << std::endl;
            return false;
        }

        // Überprüfen, ob die Blockgrößen übereinstimmen
        if (block_size_start != sizeof(header)) {
            std::cerr << "Fehler: Start- und End-Blockgrößen des Headers stimmen nicht überein!" << std::endl;
            return false;
        }
        
        // Ausgabe des Headers zur Überprüfung
        std::cout << "Gadget2 Header: " << std::endl;
        std::cout << "Gas (Typ 0): " << header.npart[0] << std::endl;
        std::cout << "Halo (Typ 1): " << header.npart[1] << std::endl;
        std::cout << "Disk (Typ 2): " << header.npart[2] << std::endl;
        std::cout << "Bulge (Typ 3): " << header.npart[3] << std::endl;
        std::cout << "Stars (Typ 4): " << header.npart[4] << std::endl;
        std::cout << "Black Hole (Typ 5): " << header.npart[5] << std::endl;

        /*
        std::cout << "Mass per type:" << std::endl;
        for (int i = 0; i < 6; ++i) { // Annahme: 6 Typen
            std::cout << "Typ " << i << ": " << header.massarr[i] << " Mass" << std::endl;
        }
        std::cout << "Boxgröße: " << header.BoxSize << std::endl;
        std::cout << "Hubble-Parameter: " << header.HubbleParam << std::endl;
        std::cout << "Roteshift: " << header.redshift << std::endl;
        std::cout << "Simulationszeit: " << header.time << std::endl;
        */
        
    
        // Gesamtanzahl der Partikel berechnen
        unsigned int total_particles = 0;
        for(int i = 0; i < 6; ++i){
            total_particles += header.npart[i];
        }

        // Reservieren des Speicherplatzes für Partikel
        particles.reserve(total_particles);

        // ### Lesen des Positionsblocks (POS) ###
        
        // Lesen der Blockgröße vor dem POS-Block
        unsigned int pos_block_size_start;
        file.read(reinterpret_cast<char*>(&pos_block_size_start), sizeof(pos_block_size_start));
        if (!file) {
            std::cerr << "Fehler: Konnte die Start-Blockgröße des POS-Blocks nicht lesen!" << std::endl;
            return false;
        }

        // Berechnen der erwarteten Größe für den POS-Block: N * 3 * sizeof(float)
        unsigned int expected_pos_block_size = total_particles * 3 * sizeof(float);
        if (pos_block_size_start != expected_pos_block_size) {
            std::cerr << "Warnung: Erwartete POS-Blockgröße (" << expected_pos_block_size 
                    << " bytes) stimmt nicht mit gelesener Größe (" << pos_block_size_start << " bytes) überein." << std::endl;
            // Optional: Fortfahren oder Abbruch
        }

        // Lesen der Positionsdaten
        std::vector<float> positions(total_particles * 3); // N * 3 floats
        file.read(reinterpret_cast<char*>(positions.data()), pos_block_size_start);
        if (!file) {
            std::cerr << "Fehler: Konnte die Positionsdaten nicht lesen!" << std::endl;
            return false;
        }

        // Lesen der Blockgröße nach dem POS-Block
        unsigned int pos_block_size_end;
        file.read(reinterpret_cast<char*>(&pos_block_size_end), sizeof(pos_block_size_end));
        if (!file) {
            std::cerr << "Fehler: Konnte die End-Blockgröße des POS-Blocks nicht lesen!" << std::endl;
            return false;
        }

        // Überprüfen, ob die Blockgrößen übereinstimmen
        if (pos_block_size_start != pos_block_size_end) {
            std::cerr << "Fehler: Start- und End-Blockgrößen des POS-Blocks stimmen nicht überein!" << std::endl;
            return false;
        }

        // ### Lesen des Geschwindigkeitsblocks (VEL) ###

        // Lesen der Blockgröße vor dem VEL-Block
        unsigned int vel_block_size_start;
        file.read(reinterpret_cast<char*>(&vel_block_size_start), sizeof(vel_block_size_start));
        if (!file) {
            std::cerr << "Fehler: Konnte die Start-Blockgröße des VEL-Blocks nicht lesen!" << std::endl;
            return false;
        }

        // Berechnen der erwarteten Größe für den VEL-Block: N * 3 * sizeof(float)
        unsigned int expected_vel_block_size = total_particles * 3 * sizeof(float);
        if (vel_block_size_start != expected_vel_block_size) {
            std::cerr << "Warnung: Erwartete VEL-Blockgröße (" << expected_vel_block_size 
                    << " bytes) stimmt nicht mit gelesener Größe (" << vel_block_size_start << " bytes) überein." << std::endl;
            // Optional: Fortfahren oder Abbruch
        }

        // Lesen der Geschwindigkeitsdaten
        std::vector<float> velocities(total_particles * 3); // N * 3 floats
        file.read(reinterpret_cast<char*>(velocities.data()), vel_block_size_start);
        if (!file) {
            std::cerr << "Fehler: Konnte die Geschwindigkeitsdaten nicht lesen!" << std::endl;
            return false;
        }

        // Lesen der Blockgröße nach dem VEL-Block
        unsigned int vel_block_size_end;
        file.read(reinterpret_cast<char*>(&vel_block_size_end), sizeof(vel_block_size_end));
        if (!file) {
            std::cerr << "Fehler: Konnte die End-Blockgröße des VEL-Blocks nicht lesen!" << std::endl;
            return false;
        }

        // Überprüfen, ob die Blockgrößen übereinstimmen
        if (vel_block_size_start != vel_block_size_end) {
            std::cerr << "Fehler: Start- und End-Blockgrößen des VEL-Blocks stimmen nicht überein!" << std::endl;
            return false;
        }

        // ### Lesen des ID-Blocks (ID) ###

        // Lesen der Blockgröße vor dem ID-Block
        unsigned int id_block_size_start;
        file.read(reinterpret_cast<char*>(&id_block_size_start), sizeof(id_block_size_start));
        if (!file) {
            std::cerr << "Fehler: Konnte die Start-Blockgröße des ID-Blocks nicht lesen!" << std::endl;
            return false;
        }

        // Berechnen der erwarteten Größe für den ID-Block: N * sizeof(unsigned int)
        unsigned int expected_id_block_size = total_particles * sizeof(unsigned int);
        if (id_block_size_start != expected_id_block_size) {
            std::cerr << "Warnung: Erwartete ID-Blockgröße (" << expected_id_block_size 
                    << " bytes) stimmt nicht mit gelesener Größe (" << id_block_size_start << " bytes) überein." << std::endl;
            // Optional: Fortfahren oder Abbruch
        }

        // Lesen der ID-Daten
        std::vector<unsigned int> ids(total_particles); // N unsigned ints
        file.read(reinterpret_cast<char*>(ids.data()), id_block_size_start);
        if (!file) {
            std::cerr << "Fehler: Konnte die ID-Daten nicht lesen!" << std::endl;
            return false;
        }

        // Lesen der Blockgröße nach dem ID-Block
        unsigned int id_block_size_end;
        file.read(reinterpret_cast<char*>(&id_block_size_end), sizeof(id_block_size_end));
        if (!file) {
            std::cerr << "Fehler: Konnte die End-Blockgröße des ID-Blocks nicht lesen!" << std::endl;
            return false;
        }

        // Überprüfen, ob die Blockgrößen übereinstimmen
        if (id_block_size_start != id_block_size_end) {
            std::cerr << "Fehler: Start- und End-Blockgrößen des ID-Blocks stimmen nicht überein!" << std::endl;
            return false;
        }

        //read mass:
        // Überprüfen, ob individuelle Massen vorhanden sind (massarr[i] == 0)
        const float epsilon = 1e-10;
        bool has_individual_mass = false;
        for(int i = 0; i < 6; ++i){
            if(header.massarr[i] < epsilon)
            {
                if(header.npart[i] != 0)
                {
                    has_individual_mass = true;
                    break;
                }
                else
                {
                    //std::cout << "Mass for type: "<< i << " is 0 because Npart is 0" << std::endl;
                }
            }
            else
            {
                //std::cout << std::scientific << "Mass for type: "<< i << " is " <<  header.massarr[i] << std::endl;
            }
        }

        std::vector<float> masses; // Vektor zur Speicherung der Massen
        if(has_individual_mass)
        {
            // Lesen der Blockgröße vor dem MASS-Block
            unsigned int mass_block_size_start;
            file.read(reinterpret_cast<char*>(&mass_block_size_start), sizeof(mass_block_size_start));
            if (!file) {
                std::cerr << "Fehler: Konnte die Start-Blockgröße des MASS-Blocks nicht lesen!" << std::endl;
                return false;
            }

            // Berechnen der erwarteten Größe für den MASS-Block: N * sizeof(float)
            unsigned int expected_mass_block_size = total_particles * sizeof(float);
            if (mass_block_size_start != expected_mass_block_size) {
                std::cerr << "Warnung: Erwartete MASS-Blockgröße (" << expected_mass_block_size 
                        << " bytes) stimmt nicht mit gelesener Größe (" << mass_block_size_start << " bytes) überein." << std::endl;
                // Optional: Fortfahren oder Abbruch
            }

            // Lesen der Massendaten
            masses.resize(total_particles);
            file.read(reinterpret_cast<char*>(masses.data()), mass_block_size_start);
            if (!file) {
                std::cerr << "Fehler: Konnte die Massendaten nicht lesen!" << std::endl;
                return false;
            }

            // Lesen der Blockgröße nach dem MASS-Block
            unsigned int mass_block_size_end;
            file.read(reinterpret_cast<char*>(&mass_block_size_end), sizeof(mass_block_size_end));
            if (!file) {
                std::cerr << "Fehler: Konnte die End-Blockgröße des MASS-Blocks nicht lesen!" << std::endl;
                return false;
            }

            // Überprüfen, ob die Blockgrößen übereinstimmen
            if (mass_block_size_start != mass_block_size_end) {
                std::cerr << "Fehler: Start- und End-Blockgrößen des MASS-Blocks stimmen nicht überein!" << std::endl;
                return false;
            }
        }

        // ### Lesen des U-Blocks (interne Energie) ###

        // Überprüfen, ob Gaspartikel vorhanden sind
        if(header.npart[0] > 0) // Nur wenn es Gaspartikel gibt
        {
            std::cout << "reading U from gas particles" << std::endl;
            // Lesen der Blockgröße vor dem U-Block
            unsigned int u_block_size_start;
            file.read(reinterpret_cast<char*>(&u_block_size_start), sizeof(u_block_size_start));
            if (!file) {
                std::cerr << "Fehler: Konnte die Start-Blockgröße des U-Blocks nicht lesen!" << std::endl;
                return false;
            }

            // Erwartete Größe des U-Blocks berechnen: Anzahl der Gaspartikel * sizeof(float)
            unsigned int expected_u_block_size = header.npart[0] * sizeof(float);
            if (u_block_size_start != expected_u_block_size) {
                std::cerr << "Warnung: Erwartete U-Blockgröße (" << expected_u_block_size 
                        << " Bytes) stimmt nicht mit gelesener Größe (" << u_block_size_start << " Bytes) überein." << std::endl;
                // Optional: Fortfahren oder Abbruch
            }

            // Lesen der U-Daten
            std::vector<float> u_values(header.npart[0]); // Interne Energie pro Masseneinheit für Gaspartikel
            file.read(reinterpret_cast<char*>(u_values.data()), u_block_size_start);
            if (!file) {
                std::cerr << "Fehler: Konnte die U-Daten nicht lesen!" << std::endl;
                return false;
            }

            // Lesen der Blockgröße nach dem U-Block
            unsigned int u_block_size_end;
            file.read(reinterpret_cast<char*>(&u_block_size_end), sizeof(u_block_size_end));
            if (!file) {
                std::cerr << "Fehler: Konnte die End-Blockgröße des U-Blocks nicht lesen!" << std::endl;
                return false;
            }

            // Überprüfen, ob die Blockgrößen übereinstimmen
            if (u_block_size_start != u_block_size_end) {
                std::cerr << "Fehler: Start- und End-Blockgrößen des U-Blocks stimmen nicht überein!" << std::endl;
                return false;
            }
        
            unsigned int current_particle = 0;
            unsigned int gas_particle_index = 0;
            int count_gas = 0;
            int count_dark = 0;
            int count_star = 0;

            for(int type = 0; type < 6; ++type)
            {
                    for(unsigned int i = 0; i < (unsigned int)header.npart[type]; ++i){
                        if(current_particle >= total_particles){
                            std::cerr << "Fehler: Überschreitung der Partikelanzahl beim Erstellen der Partikel!" << std::endl;
                            return false;
                        }
                        auto particle = std::make_shared<Particle>();
                        particle->id = ids[current_particle];

                        // Setzen des Partikeltyps und der Masse
                        if(type == 1)  
                        {   
                            particle->type = 3; // dark matter
                            particle->galaxyPart = 3; // Halo
                            count_dark++;
                        }
                        else if(type == 2 || type == 4 || type == 5) 
                        {
                            particle->type = 1;
                            count_star++;
                        }
                        else if (type == 3)
                        {
                            particle->type = 1;
                            particle->galaxyPart = 2; // Bulge
                            count_star++;
                        }
                        else if(type == 0) 
                        {
                            particle->type = 2; // Gas
                            particle->galaxyPart = 1; // Disk
                            count_gas++;
                        }

                        if(has_individual_mass)
                        {
                            particle->mass = masses[current_particle] * Units::MSUN * 1e10;
                        }
                        else
                        {
                            particle->mass = header.massarr[type] * Units::MSUN * 1e10;
                        }

                        particle->position = vec3(
                            positions[3*current_particle],
                            positions[3*current_particle + 1],
                            positions[3*current_particle + 2]
                        );
                        particle->velocity = vec3(
                            velocities[3*current_particle],
                            velocities[3*current_particle + 1],
                            velocities[3*current_particle + 2]
                        );

                        // Skalierung auf SI-Einheiten
                        particle->position *= Units::KPC;
                        particle->velocity *= Units::KMS;

                        // Interne Energie für Gaspartikel zuweisen
                        if(type == 0) // Gaspartikel
                        {
                            //scale to SI
                            particle->U = u_values[gas_particle_index] * 1e6;
                            //std::cout << particle->U << std::endl;
                            gas_particle_index++;
                        }

                        particles.push_back(particle);
                        current_particle++;
                    }
                }
            }
        else // Wenn keine Gaspartikel vorhanden sind
        {
            // Ihr bestehender Code zum Erstellen der Partikel
            unsigned int current_particle = 0;
            int count_gas = 0;
            int count_dark = 0;
            int count_star = 0;

            for(int type = 0; type < 6; ++type){
                for(unsigned int i = 0; i < (unsigned int)header.npart[type]; ++i){
                    if(current_particle >= total_particles){
                        std::cerr << "Fehler: Überschreitung der Partikelanzahl beim Erstellen der Partikel!" << std::endl;
                        return false;
                    }
                    auto particle = std::make_shared<Particle>();
                    particle->id = ids[current_particle];

                    // Setzen des Partikeltyps und der Masse
                    if(type == 1)  
                    {   
                        particle->type = 3; // dark matter
                        particle->galaxyPart = 3; // Halo
                        count_dark++;
                    }
                    else if(type == 2 || type == 4 || type == 5) 
                    {
                        particle->type = 1;
                        count_star++;
                    }
                    else if (type == 3)
                    {
                        particle->type = 1;
                        particle->galaxyPart = 2; // Bulge
                        count_star++;
                    }
                    else if(type == 0) 
                    {
                        particle->type = 2; // Gas
                        particle->galaxyPart = 1; // Disk
                        count_gas++;
                    }

                    if(has_individual_mass)
                    {
                        particle->mass = masses[current_particle] * Units::MSUN * 1e10;
                    }
                    else
                    {
                        particle->mass = header.massarr[type] * Units::MSUN * 1e10;
                    }

                    particle->position = vec3(
                        positions[3*current_particle],
                        positions[3*current_particle + 1],
                        positions[3*current_particle + 2]
                    );
                    particle->velocity = vec3(
                        velocities[3*current_particle],
                        velocities[3*current_particle + 1],
                        velocities[3*current_particle + 2]
                    );

                    // Skalierung auf SI-Einheiten
                    particle->position *= Units::KPC;
                    particle->velocity *= Units::KMS;

                    particles.push_back(particle);
                    current_particle++;
                }
            }
        }

        int num_stars = 0;
        int num_gas = 0;
        int num_dark = 0;

        int num_disk = 0;
        int num_bulge = 0;
        int num_halo = 0;

        for(auto particle : particles)
        {
            if(particle->type == 1)
            {
                num_stars++;
            }
            else if(particle->type == 2)
            {
                num_gas++;
            }
            else if(particle->type == 3)
            {
                num_dark++;
            }

            if(particle->galaxyPart == 1)
            {
                num_disk++;
            }
            else if(particle->galaxyPart == 2)
            {
                num_bulge++;
            }
            else if(particle->galaxyPart == 3)
            {
                num_halo++;
            }
        }
        std::cout << "\nParticle types: " << std::endl;
        std::cout << "stars: " << num_stars << std::endl;
        std::cout << "gas: " << num_gas << std::endl;
        std::cout << "dark matter: " << num_dark << std::endl;
        std::cout << "Galaxy parts: " << std::endl;
        std::cout << "disk: " << num_disk << std::endl;
        std::cout << "bulge: " << num_bulge << std::endl;
        std::cout << "halo: " << num_halo << std::endl;

        // Datei schließen
        file.close();

        std::cout << "gadget2 snapshot sucessfully read." << std::endl;
        return true;
    }
    else
    {
        std::cout << "invalid input data format: " << inputFormat << std::endl;
        return false;
    }
    return false;
}

// Hilfsfunktion zum Entfernen von Leerzeichen am Anfang und Ende eines Strings
std::string trim(const std::string& str) {
    std::string trimmed = str;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), trimmed.end());
    return trimmed;
}

bool parseKeyValue(const std::string& line, std::string& key, std::string& value) {
    std::size_t pos = line.find('=');
    if (pos == std::string::npos) return false;

    key = line.substr(0, pos);
    value = line.substr(pos + 1);

    // Entferne Leerzeichen
    key = trim(key);
    value = trim(value);

    return !key.empty() && !value.empty();
}


bool DataManager::loadConfig(const std::string& filename, Simulation* simulation) 
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Fehler beim Öffnen der Konfigurationsdatei: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Ignoriere leere Zeilen und Kommentare
        if (line.empty() || line[0] == '#') continue;

        // Trim die Zeile, um führende und nachfolgende Leerzeichen zu entfernen
        std::string trimmedLine = trim(line);

        // Ignoriere leere Zeilen und Kommentare
        if (trimmedLine.empty() || trimmedLine[0] == '#') continue;

        std::string key, value;
        if (parseKeyValue(line, key, value)) {
            // Entferne führende und nachfolgende Leerzeichen
            key = trim(key);
            value = trim(value);
            try {
                if (key == "numberOfParticles") simulation->numberOfParticles = std::stod(value);
                else if (key == "eta") simulation->eta = std::stod(value);
                else if (key == "maxTimeStep") simulation->maxTimeStep = std::stod(value);
                else if (key == "minTimeStep") simulation->minTimeStep = std::stod(value);
                else if (key == "globalTime") simulation->globalTime = std::stod(value);
                else if (key == "endTime") simulation->endTime = std::stod(value);
                else if (key == "fixedTimeSteps") simulation->fixedTimeSteps = std::stod(value);
                else if (key == "e0") simulation->e0 = std::stod(value);
                else if (key == "massInH") simulation->massInH = std::stod(value);
                else if (key == "H0") simulation->H0 = std::stod(value);
                else if (key == "theta") simulation->theta = std::stod(value);
                else if (key == "inputPath") inputPath = value;
                else if (key == "inputDataFormat") inputFormat = value;
                else if (key == "outputFolderName") outputPath += (value + "/");
                else if (key == "outputDataFormat") outputFormat = value;
                else {
                    std::cerr << "unknown key: " << key << std::endl;
                    return false;
                }
            } catch (const std::invalid_argument& e) {
                std::cerr << "Ungültiger Wert für " << key << ": " << value << std::endl;
                return false;
            } catch (const std::out_of_range& e) {
                std::cerr << "Wert für " << key << " ist außerhalb des gültigen Bereichs: " << value << std::endl;
                return false;
            }
        }
    }

    return true;
}