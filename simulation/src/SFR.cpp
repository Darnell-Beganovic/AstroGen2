#include "SFR.h"
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include "Constants.h"
#include <random>
#include "Simulation.h"
#include <algorithm>    

double randomUniform() {return static_cast<double>(rand()) / RAND_MAX;}

void SFR::sfrRoutine(Particle* particle, Simulation* sim, double& newStarMass)
{
    if(!particle) return;
    if(particle->rho <= 0) return;
    if(particle->U <= 0) return;
    if(particle->mass <= 0) return;

    double densityThreshold = 1e-23;
    double temperatureThreshold = 1e4;

    if (particle->rho < densityThreshold || particle->T > temperatureThreshold) return;

    double c = 0.01;
    double tdyn = sqrt(3 * M_PI / (32 * Constants::G * particle->rho));
    double dt = particle->timeStep;
    
    double p_star = 1.0 - std::exp(-c * dt / tdyn);
    
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    random::normal_dist();
    double r = dist(rng);
    
    if (r < p_star)
    {
        double efficiency = 0.3;
        double starMass = efficiency * particle->mass;
        newStarMass += starMass;

        Particle* newStar = new Particle();
        newStar->mass = starMass;
        newStar->position = particle->position;
        newStar->velocity = particle->velocity;
        newStar->type = 1;
        newStar->galaxyPart = particle->galaxyPart;
        newStar->id = sim->particles.size();

        double p_supernova = 0.12;
        double r_sn = dist(rng);
        if (r_sn < p_supernova)
        {
            newStar->SN_pending = true;
            newStar->h = particle->h;
            newStar->rho = particle->rho;
            newStar->P = particle->P;
            newStar->T = particle->T;
            newStar->U = particle->U;
            newStar->mu = particle->mu;
        }
        else
        {
            newStar->SN_pending = false;
        }
        #pragma omp critical
        {
            sim->particles.push_back(newStar);
        }
        
        particle->mass -= starMass;
        const double minGasMass = 1e-5;
        if (particle->mass < minGasMass) 
        {
            auto it = std::find(sim->particles.begin(), sim->particles.end(), particle);
            if (it != sim->particles.end()) 
            {
                delete *it;
                #pragma omp critical
                {
                    sim->particles.erase(it);
                }
            }
        }
    }
}