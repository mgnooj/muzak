#define OLC_PGE3_APPLICATION
#include "olcPixelGameEngine3.h"
#include "miniaudio.h"

#if defined(__PGETINKER__)
#include "pgetinker.h"
#else
static inline void pgetinker_file_resolve(const char* url, const char* mountPath) {}
#endif

// TODO
// Planet interactions: try
	// Relative deformation
	// Add NESW pole controls
// Audio
	// Vector with 'synths': id == a planet id / the sun
		// Each synth has two waveforms, a drone and a arp; and a master delay
	// Master LPF
// Game progression
// Menus
// Details
	// Layer for background, flares, editor lines
	// Shaders + effects
	// Starfield background
	// Dev mode

const olc::vf2d 	SCREENSIZE = { 500.f,500.f };
const float 		SCREEN_DIAGONAL_SQUARED = powf(SCREENSIZE.x, 2.f) + powf(SCREENSIZE.y, 2.f);
float 				MU = 12000.f;
const float 		TWO_PI = 2.f * 3.14159f;
const olc::vf2d 	CENTER = { SCREENSIZE.x / 2.f, SCREENSIZE.y / 2.f };
const float 		TAIL_WIDTH = 0.5f;
const int 			TAIL_LENGTH = 250;

enum starState {
    T_TAURI, // 100 million : lasts 1 minutes
	MATURE, // 10 billion : lasts 6 minutes
	REDGIANT, // 1 billion : lasts 2 minutes
	NEBULA, // 100 million : lasts 30 seconds
	WHITEDWARF
};

struct Particle
{
    olc::vf2d position;
    olc::vf2d velocity;
    olc::Pixel color;
    float life;
};

struct Body {
public:
	float radius;
	olc::vf2d position;
	int id;

	Body() {}
};

struct SolarFlare: public Body {
public:
	float speed;
	float bloom; // overlayCircleRadius = radius - bloom
    olc::Pixel color;

	SolarFlare(float rad, int newId, float sp, float bl, olc::Pixel hue) {
		radius = rad;
		position = { 0.f, 0.f };
		id = newId;
		speed = sp;
		bloom = bl;
		color = hue;
	}

	void update(float dt) {
		radius += dt * speed;
	}

	bool exceedsScreen() {
		return powf((radius - bloom), 2.f) * 2.f > SCREEN_DIAGONAL_SQUARED;
	}

	Body getBloomShape() {
		Body bloomShape;
		bloomShape.radius = radius - bloom;
		bloomShape.position = position;
		bloomShape.id = id;
		return bloomShape;
	}

	// To find if something is 'in' the flare:
		// return  overlap(flare, body) && !contains(bloom, body)
};

struct Star : public Body {
public:
    starState starState = T_TAURI;
	Star() {
		radius = 20.f;
		position = { 0.f, 0.f };
		id = -1;
	}
};

struct MovingBody : public Body {
public:
	std::vector<olc::vf2d> path = {};

	MovingBody() {}

	virtual void update(float dt) {}
};

struct Planet : public MovingBody {
public:
	float phase;
	float speed;
	float orbitRadius;			// semi-major (x) axis length
	float orbitEccentricity;	// semi-minor (y) axis length
	std::vector<olc::vf2d> orbitOutline;
	bool isHighlighted = false;

	Planet(float rad, float startPhase, int newId, float sp, float orbRad, float orbEcc) {
		radius = rad;
		phase = startPhase * TWO_PI;
		id = newId;
		speed = sp;
		orbitRadius = orbRad;
		orbitEccentricity = orbEcc;
		calculateOutline();
	}

	void calculateOutline() {
		orbitOutline.clear();
		auto step = TWO_PI / 100.f;
		auto outlinePhase = 0.f;
		for (auto i = 0; i < 100; i++) {
			orbitOutline.push_back(
				{ orbitRadius * std::cos(outlinePhase), orbitEccentricity * std::sin(outlinePhase) }
			);
			outlinePhase += step;
		}
	}

	void modifyOrbit(olc::vf2d newShape, olc::vf2d offset) {
		orbitRadius = abs(newShape.x + offset.x);
		orbitEccentricity = abs(newShape.y + offset.y);
		calculateOutline();
	}

	void update(float dt) override {
        // ellipse formula: https://en.wikipedia.org/wiki/Ellipse#Parametric_representation
		float newPhase = phase + (dt * speed);
        if (newPhase > TWO_PI)
            newPhase = 0.f;
		phase = newPhase;
		position = { orbitRadius * std::cos(newPhase), orbitEccentricity * std::sin(newPhase) };

		path.insert(path.begin(), position);
        if (path.size() > TAIL_LENGTH)
            path.pop_back();
	}
};

struct Asteroid : public MovingBody {
public:
	olc::vf2d velocity;

	Asteroid(float rad, olc::vf2d pos, int newId, olc::vf2d vel) {
		radius = rad;
		position = pos;
		id = newId;
		velocity = vel;
	}

	olc::vf2d accelerationVector() {
		// Two-body gravity formula mostly derived from
		// https://www.mysimulator.uk/content/tutorials/orbit-simulation-50-lines-js.html
		const float r2 = position.x * position.x + position.y * position.y;
		const float r = std::sqrt(r2);
		const float f = -MU / (r2 * r);
		return { f * position.x, f * position.y };
	}

	void update(float dt) override {
		const auto a0 = accelerationVector();
		position.x += velocity.x * dt + 0.5f * a0.x * dt * dt;
		position.y += velocity.y * dt + 0.5f * a0.y * dt * dt;
		const auto a1 = accelerationVector();
		velocity.x += 0.5f * (a0.x + a1.x) * dt;
		velocity.y += 0.5f * (a0.y + a1.y) * dt;

		path.insert(path.begin(), position);
        if (path.size() > TAIL_LENGTH)
            path.pop_back();
	}
};

enum SoundComponentTarget {
	DRONE, ARP
};

struct PlanetSoundComponent {
	int id;

	miniaudio::ma_waveform 			drone;			// Sine
    miniAudio::ma_waveform_config 	droneConfig;
	miniAudio::ma_delay_node    	droneDelay;

	miniaudio::ma_waveform 			arp;			// Square
    miniAudio::ma_waveform_config 	arpConfig;
	miniAudio::ma_delay_node    	arpDelay;

	PlanetSoundComponent() {
		// Initialize everything
	}
};

struct MainSoundComponent {
	miniaudio::ma_node_graph    		g_nodeGraph;
	miniaudio::ma_lpf_node      		g_lpfNode;
	std::vector<PlanetSoundComponent> 	synths;
	
	MainSoundComponent() {
		for (auto i = 0; i < 10; i++) {
			// synths.append(PlanetSoundComponent());
		}

		// Setup node graph
        ma_node_graph_config nodeGraphConfig = ma_node_graph_config_init(CHANNELS);

        result = ma_node_graph_init(&nodeGraphConfig, NULL, &g_nodeGraph);
        if (result != MA_SUCCESS) {
            printf("ERROR: Failed to initialize node graph.");
            return -1;
        }

		// Set up LPF
        ma_lpf_node_config lpfNodeConfig = ma_lpf_node_config_init(CHANNELS, SAMPLE_RATE, SAMPLE_RATE / LPF_CUTOFF_FACTOR, LPF_ORDER);

        result = ma_lpf_node_init(&g_nodeGraph, &lpfNodeConfig, NULL, &g_lpfNode);
        if (result != MA_SUCCESS) {
            printf("ERROR: Failed to initialize low pass filter node.");
            return -1;
        }

        /* Connect the output bus of the low pass filter node to the input bus of the endpoint. */
        ma_node_attach_output_bus(&g_lpfNode, 0, ma_node_graph_get_endpoint(&g_nodeGraph), 0);

        /* Set the volume of the low pass filter to make it more of less impactful. */
        ma_node_set_output_bus_volume(&g_lpfNode, 0, LPF_BIAS);
	}

	void updateFrequency() {}
	void updateAmplitude() {}
	void play() {}
	void stop() {}
	void updateDelay() {}
	void updateFeedback() {}
}

class MuzakOfTheSpheres : public olc::PixelGameEngine
{
public:
    float totalElapsedTime;
    Star sun;
    std::vector<std::unique_ptr<Planet>> planets;
    std::vector<std::unique_ptr<SolarFlare>> flares;
	std::vector<olc::vf2d> explosions;
	std::vector<Particle> vecParticles;

	bool mouseHeld = false;

	bool highlightingPlanet = false;
	Planet* highlightedPlanet;

	bool modifyingPlanet = false;
	Planet* modifiedPlanet;
	olc::vf2d clickOffset;

    MuzakOfTheSpheres()
    {
        sAppName = "Muzak of the Spheres";
		planets.push_back(std::make_unique<Planet>(15.f, 0.3f, 0, 1.3f, 100.f, 100.f));
		planets.push_back(std::make_unique<Planet>(7.f, 0.f, 1, 0.7f, 200.f, 50.f));
		planets.push_back(std::make_unique<Planet>(9.f, 0.66f, 2, 2.f, 300.f, 50.f));
    }

	bool OnUserCreate() override
	{
		return true;
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
		totalElapsedTime += fElapsedTime;

		if (fmod(totalElapsedTime, 10.f) == 0.f) {	// DEBUG
            std::cout << "Emitted flare" << std::endl;
			flares.push_back(std::make_unique<SolarFlare>(20.f, 0, 10.f, 10.f, olc::Colour::RED));
		}
		drawState(fElapsedTime);

        return true;
    }

    void checkForCollisions() {
        if (planets.empty() || planets.size() == 1) return;
        for (auto idx = 0; idx < planets.size() - 1; idx++) {
			auto body1 = planets[idx].get();
            for (auto idx2 = idx+1; idx2 < planets.size(); idx2++) {
                auto body2 = planets[idx2].get();
                if (areColliding(body1, body2)) {
                    handleCollision(body1, body2);
                }
            }
        }
    }

	void checkForSolarFlareActivations() {
        for (auto idx = 0; idx < flares.size(); idx++) {
			auto flare = flares[idx].get();
            for (auto idx2 = 0; idx2 < planets.size(); idx2++) {
                auto body = planets[idx2].get();
                if (isInSolarFlare(flare, body)) {
                    handleSolarActivation(body);
                }
            }
		}
	}

	bool isInSolarFlare(SolarFlare* flare, Body* body) {
		Body bloom = flare->getBloomShape();
		bool collided = areColliding(flare, body);
		bool contained = !contains(&bloom, body);
		return collided && contained;
	}

	void handleUserInput() {

        bool mouseDown = mouse.GetButton(0).bHeld;
		bool newClick = mouseDown && !mouseHeld;
		mouseHeld = mouseDown;
		olc::vf2d mousePosition = mouse.GetPosition().round();
		olc::vf2d adjustedMousePosition = { mousePosition.x - SCREENSIZE.x / 2.f, mousePosition.y - SCREENSIZE.y / 2.f };

		if (newClick && highlightingPlanet) {
			highlightingPlanet = false;
			modifyingPlanet = true;
			modifiedPlanet = highlightedPlanet;
			return;
		} 

		if (mouseDown && modifyingPlanet) {
			modifiedPlanet->modifyOrbit(adjustedMousePosition, clickOffset);
			return;
		}

		if (!mouseDown) {
			modifyingPlanet = false;
		}

		if (highlightingPlanet) {
			for (auto& orbitPoint : highlightedPlanet->orbitOutline) {
				auto dist = distance(orbitPoint, adjustedMousePosition);
				if (dist < 20.f) {
					clickOffset = { orbitPoint.x - adjustedMousePosition.x, orbitPoint.y - adjustedMousePosition.y };
					return;
				}
			}
			highlightingPlanet = false;
			highlightedPlanet->isHighlighted = false;
		} else {
			for (auto& planetPtr : planets) {
				auto planet = planetPtr.get();
				for (auto& orbitPoint : planet->orbitOutline) {
					auto dist = distance(orbitPoint, adjustedMousePosition);
					if (dist < 20.f) {
						highlightingPlanet = true;
						highlightedPlanet = planet;
						planet->isHighlighted = true;
						clickOffset = { orbitPoint.x - adjustedMousePosition.x, orbitPoint.y - adjustedMousePosition.y };
						return;
					}
				}
			}
		}

		/*
        bool mouseClicked = mouse.GetButton(0).bHeld;

		if (mouseClicked) {
			olc::vf2d click = mouse.GetPosition().round();
			olc::vf2d adjustedClick = { click.x - SCREENSIZE.x / 2.f, click.y - SCREENSIZE.y / 2.f };
			for (auto& bodyPtr : planets) {
				auto body = bodyPtr.get();
				if (isClicked(body, adjustedClick)) {
					if (alreadyClicking && body->id == clickedPlanetId) {
						// TODO: Draw ellipsis
						body->orbitRadius = abs(adjustedClick.x) / 2.f; //abs(adjustedClick.x + clickOffset.x);
						body->orbitEccentricity = abs(adjustedClick.x) / 2.f; //abs(adjustedClick.y + clickOffset.y);
					} else if (alreadyClicking) {
						deactivatePlanet(clickedPlanetId);
						body->isEditing = true;
						clickedPlanetId = body->id;
						clickOffset = { adjustedClick.x - body->position.x, adjustedClick.y - body->position.y };
					} else {
						alreadyClicking = true;
						body->isEditing = true;
						clickedPlanetId = body->id;
						clickOffset = { adjustedClick.x - body->position.x, adjustedClick.y - body->position.y };
					}
					return;
				}
			}
		} else {
			if (alreadyClicking) {
				deactivatePlanet(clickedPlanetId);
				alreadyClicking = false;
			}
		}
		*/
	}
	
	void handleSolarActivation(Body* body) {}

	void drawState(float dt) {
		draw.WorldOffset(CENTER);
        draw.Clear(olc::Colour::BLACK);
		handleUserInput();
        for (auto& bodyPtr : planets) {
            auto body = bodyPtr.get();
            body->update(dt);
        }
        for (auto& flarePtr : flares) {
            auto flare = flarePtr.get();
            flare->update(dt);
        }
        checkForCollisions();
		checkForSolarFlareActivations();
        for (auto& flarePtr : flares) {
            auto flare = flarePtr.get();
            draw.FilledCircle(flare->position, flare->radius, flare->color);
			draw.FilledCircle(flare->position, flare->radius - flare->bloom, olc::Colour::BLACK);
        }
        draw.FilledCircle({0.f, 0.f}, sun.radius, olc::Colour::TANGERINE);
        for (auto& bodyPtr : planets) {
            auto body = bodyPtr.get();
            draw.FilledCircle(body->position, body->radius, olc::Colour::BLUE);
			if (body->isHighlighted) {
				for (auto& point : body->orbitOutline) {
					draw.FilledCircle(point, 0.5f, olc::Colour::WHITE);
				}
			}
        }
		handleExplosions(dt);
		draw.WorldReset();
	}

	void handleExplosions(float dt) {
        for (auto& explosion : explosions) {
            spawnExplosionParticles(explosion);
        }
        explosions.clear();
		for (auto& particle : vecParticles) {
            particle.life -= dt;
            particle.position.x += particle.velocity.x;
            particle.position.y += particle.velocity.y;
            draw.FilledCircle(particle.position, 2.f, particle.color);
		}
		vecParticles.erase(
			std::remove_if(vecParticles.begin(), vecParticles.end(),
			[](Particle p) -> bool { return p.life <= 0.0f; }),
			vecParticles.end()
		);
	}

	float distance(olc::vf2d& body1, olc::vf2d& body2) {
		return sqrt(powf(body2.x - body1.x, 2.f) + powf(body2.y - body1.y, 2.f));
	}

	bool areColliding(Body* body1, Body* body2) {
		float dist = distance(body1->position, body2->position);
		return (dist >= abs(body1->radius - body2->radius) && dist <= body1->radius + body2->radius);
	}

	bool contains(Body* body1, Body* body2)
	{
		return (std::sqrt(std::pow(body2->position.x - body1->position.x, 2) + std::pow(body2->position.y - body1->position.y, 2)) + body2->radius) <= body1->radius;
	}

	bool isClicked(Body* body, olc::vf2d point)
	{
		olc::vf2d relativePoint = { body->position.x - point.x, body->position.y - point.y };
		float mag2 = relativePoint.x * relativePoint.x + relativePoint.y * relativePoint.y;
		return mag2 <= (body->radius * body->radius);
	}

	void handleCollision(Body* body1, Body* body2) {
		if (body1->radius > body2->radius) {
			explosions.push_back(body2->position);
			removeEntity(body2->id);
		} else if (body1->radius < body2->radius) {
			explosions.push_back(body1->position);
			removeEntity(body1->id);
		} else {
			explosions.push_back(body1->position);
			removeEntity(body1->id);
			explosions.push_back(body2->position);
			removeEntity(body2->id);
		}
	}

	void removeEntity(int id) {
		for (auto idx = 0; idx < planets.size(); idx++) {
			auto body = planets[idx].get();
			if (body->id == id) {
				planets.erase(std::next(planets.begin(), idx));
				return;
			}
		}
	}

	const std::vector<olc::Pixel> colors = {
		olc::Colour::RED,
		olc::Colour::YELLOW,
		olc::Colour::TANGERINE,
		olc::Colour::MAGENTA,
		olc::Colour::GREEN,
		olc::Colour::CYAN,
		olc::Colour::BLUE,
		olc::Colour::GREY,
		olc::Colour::DARK_YELLOW,
		olc::Colour::DARK_GREEN,
		olc::Colour::DARK_BLUE,
		olc::Colour::WHITE
	};

	void spawnExplosionParticles(olc::vf2d pos)
	{
		int count = 12;
        for(int i = 0; i < count; i++)
		{
			olc::Pixel color = colors[i];
			float angle = (rand() / (float)RAND_MAX) * 2.0f * 3.14f;
            float speed = 5.0f + (rand() / (float)RAND_MAX);
            
            Particle p;
            p.position = pos;
            p.velocity = olc::vf2d{
				std::cos(angle) * speed,
				std::sin(angle) * speed
			};
            p.color = color;
            p.life = 1.0f + (rand() / (float)RAND_MAX);
            
            vecParticles.push_back(p);
        }
	}
};

int main()
{
	MuzakOfTheSpheres game;

	olc::PGEConfig config;
	config.bVSync = false;
	config.vPixelSize = { 1,1 };
	config.vScreenSize = SCREENSIZE;

	if (game.Construct(config))
		game.Start();

	return 0;
}