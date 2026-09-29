#define OLC_PGE3_APPLICATION
#include "olcPixelGameEngine3.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#if defined(__PGETINKER__)
#include "pgetinker.h"
#else
static inline void pgetinker_file_resolve(const char* url, const char* mountPath) {}
#endif

// TODO
// Spawn planets, asteroids
// Planet interactions: try
	// Relative deformation
	// Add NESW pole controls
// Refine note system
// Popup fade-in
// Shaders + effects
// Start menu

#define FORMAT              ma_format_f32
#define CHANNELS            2
#define SAMPLE_RATE         48000
#define LPF_BIAS            0.9f
#define LPF_CUTOFF_FACTOR   80
#define LPF_ORDER           8
#define DELAY_IN_SECONDS    0.2f
#define DECAY               0.5f

const olc::vf2d 	SCREENSIZE = { 500.f,500.f };
const float 		SCREEN_RADIUS_SQUARED = powf(SCREENSIZE.x / 2.f, 2.f) + powf(SCREENSIZE.y / 2.f, 2.f);
float 				MU = 12000.f;
const float 		TWO_PI = 2.f * 3.14159f;
const olc::vf2d 	CENTER = { SCREENSIZE.x / 2.f, SCREENSIZE.y / 2.f };
const float 		TAIL_WIDTH = 0.5f;
const int 			TAIL_LENGTH = 250;

float randomFloat() {
	return rand() / (float)RAND_MAX;
}

struct SolarEpoch {
	std::string name;
	std::string year;
	float start;
	float end;
	float radius;
	olc::Pixel color;
	float flareRegularity;
	float flareSpeed;
	float flareBloom;
	olc::Pixel flareColor;
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
	olc::Pixel color = olc::Colour::WHITE;

	Body() {}
};

struct SolarFlare: public Body {
public:
	float speed;
	float bloom; // overlayCircleRadius = radius - bloom
    olc::Pixel color;
	olc::Pixel bloomColor = olc::Colour::BLACK;

	SolarFlare(float rad, int newId, float sp, float bl, olc::Pixel hue) {
		radius = rad;
		position = { 0.f, 0.f };
		id = newId;
		speed = sp;
		bloom = bl;
		color = hue;
	}

	void update(float dt) {
		float step = dt * speed;
		radius += step;
		float scalar = 1.f - powf((radius - bloom),2.f) / SCREEN_RADIUS_SQUARED;
		color.a = scalar * 200;
		bloomColor.a = scalar * 200 + 55;
	}

	bool exceedsScreen() {
		return bloomColor.a < 1;
	}

	Body getBloomShape() {
		Body bloomShape;
		bloomShape.radius = radius - bloom;
		bloomShape.position = position;
		bloomShape.id = id;
		return bloomShape;
	}
};

struct Star : public Body {
	int currentEpochIndex = 0;
	float currentEpochStart;
	float currentEpochEnd;

	float flareRegularity;
	float timeSinceLastFlare = 0.f;
	bool shouldFlare = false;
	float flareSpeed;
	float flareBloom;
	olc::Pixel flareColor;
	olc::Pixel targetColor;
	float targetRadius;

	bool transitioning = false;
	bool showingPopup = true;
	float timeShowingPopup = 0.f;
	std::string epochName;
	std::string epochYears;
	bool completedLifecycle = false;

	const std::vector<SolarEpoch> epochs = {
		// 	name			age						start, 	end, 	radius, color, 					f_reg, 	f_sp, 	f_blm, 	f_color
		{ 	"T-Tauri",		"Newborn",				0.f, 	25.f, 	12.f, 	olc::Colour::YELLOW, 	8.f, 	50.f, 	2.f, 	olc::Colour::TANGERINE },
		{ 	"Mature",		"100 million years",	25.f, 	50.f, 	15.f, 	olc::Colour::TANGERINE, 8.f, 	75.f, 	5.f, 	olc::Colour::RED },
		{ 	"Red Giant",	"12 billion years",		50.f, 	75.f, 	24.f, 	olc::Colour::RED, 		8.f, 	120.f, 	10.f, 	olc::Colour::DARK_RED },
		{ 	"Nebula",		"13 billion years",		75.f, 	100.f, 	12.f, 	olc::Colour::BLUE, 		8.f, 	100.f, 	5.f, 	olc::Colour::WHITE },
		{ 	"White dwarf",	"13.2 billion years",	100.f, 	9999.f, 6.f, 	olc::Colour::WHITE, 	8.f, 	75.f, 	1.f, 	olc::Colour::YELLOW }
	};

	Star() {
		updateEpoch();
		radius = targetRadius;
		color = targetColor;
		position = { 0.f, 0.f };
		id = 0;
	}

	void update(float dt, float totalElapsedTime) {
		if (transitioning) { 
			stepTransition(); 
		}
		else if (totalElapsedTime >= currentEpochEnd) {
			currentEpochIndex += 1;
			completedLifecycle = currentEpochIndex == 4;
			updateEpoch();
			transitioning = true;
			showingPopup = true;
		}
		timeSinceLastFlare += dt;
		if (showingPopup) {
			timeShowingPopup += dt;
			if (timeShowingPopup > 10.f) {
				showingPopup = false;
				timeShowingPopup = 0.f;
			}
		}
		if (timeSinceLastFlare >= flareRegularity) {
			shouldFlare = true;
			timeSinceLastFlare = 0.f;
		}
	}

	void updateEpoch() {
		auto newEpoch = epochs[currentEpochIndex];
		currentEpochStart = newEpoch.start;
		currentEpochEnd = newEpoch.end;
		flareRegularity = newEpoch.flareRegularity;
		flareSpeed = newEpoch.flareSpeed;
		flareBloom = newEpoch.flareBloom;
		flareColor = newEpoch.flareColor;
		targetRadius = newEpoch.radius;
		targetColor = newEpoch.color;
		epochName = newEpoch.name;
		epochYears = newEpoch.year;
	}

	void stepTransition() {
		bool finishedColorChange = color == targetColor;
		bool finishedRadiusResize = abs(radius - targetRadius) < 0.1;
		if (!finishedColorChange) {
			changeColor();
		}
		if (!finishedRadiusResize) {
			if (radius < targetRadius) {
				radius += 0.1f;
			} else {
				radius -= 0.1f;
			}
		}
		if (finishedColorChange && finishedRadiusResize) {
			transitioning = false;
		}
	}

	void changeColor() {
		if (targetColor.r > color.r) {
			color.r += 1;
		}
		else if (targetColor.r < color.r) {
			color.r -= 1;
		}
		if (targetColor.b > color.b) {
			color.b += 1;
		}
		else if (targetColor.b < color.b) {
			color.b -= 1;
		}
		if (targetColor.g > color.g) {
			color.g += 1;
		}
		else if (targetColor.g < color.g) {
			color.g -= 1;
		}
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
	bool isActivated = false;

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

struct BackgroundStar {
	olc::vf2d position;
	bool activated;
};

struct Background {
	std::vector<BackgroundStar> starfield;
	std::vector<int> activatedStars;

	Background() {
		for (auto i = 0; i < 128; i++) {
            BackgroundStar newStar = { { (randomFloat() * SCREENSIZE.x) - CENTER.x, (randomFloat() * SCREENSIZE.y) - CENTER.y }, false };
			starfield.push_back(newStar);
		}
	}

	void update() {
		float a = randomFloat();
		if (a > 0.8f) {
			auto i = (int)(randomFloat() * 127);
			starfield[i].activated = true;
			activatedStars.push_back(i);
		}
		a = randomFloat();
		if (a > .95) {
			auto i = (int)(randomFloat() * activatedStars.size());
			starfield[activatedStars[i]].activated = false;
			activatedStars.erase(std::next(activatedStars.begin(), i));
		}
	}
};

enum SoundComponentTarget {
	DRONE, ARP
};

struct PlanetSoundComponent {
	int id;

	ma_waveform 			drone;			// Sine
	ma_waveform 			arp;			// Square
    ma_data_source_node  	droneNode;
    ma_data_source_node  	arpNode;
	ma_delay_node    		delay;

	PlanetSoundComponent(int newId) {
		id = newId;
	}
};

struct MainSoundComponent {
	inline static ma_node_graph    										nodeGraph;
	inline static ma_lpf_node      										lowPass;
    inline static ma_device 											device;
	inline static std::vector<std::unique_ptr<PlanetSoundComponent>> 	synths;
	inline static std::vector<int> arpsToPlay = {};
	
	MainSoundComponent() {
		// Set up node graph
    	ma_result result;
        ma_node_graph_config nodeGraphConfig = ma_node_graph_config_init(CHANNELS);
    	if (ma_node_graph_init(&nodeGraphConfig, NULL, &nodeGraph) && !MA_SUCCESS) {
			printf("Failed to initialize nodegraph.\n");
		}

		// Set up LPF
        ma_lpf_node_config lpfNodeConfig = ma_lpf_node_config_init(CHANNELS, SAMPLE_RATE, SAMPLE_RATE / LPF_CUTOFF_FACTOR, LPF_ORDER);
       	if (ma_lpf_node_init(&nodeGraph, &lpfNodeConfig, NULL, &lowPass) && !MA_SUCCESS) {
			printf("Failed to initialize LPF.\n");
		}
        ma_node_attach_output_bus(&lowPass, 0, ma_node_graph_get_endpoint(&nodeGraph), 0);
        ma_node_set_output_bus_volume(&lowPass, 0, LPF_BIAS);

		// Set up each instrument
		for (auto i = 0; i < 10; i++) {
			synths.push_back(std::make_unique<PlanetSoundComponent>(i));
			auto synth = synths[i].get();

			// Delay
			ma_delay_node_config delayNodeConfig = ma_delay_node_config_init(CHANNELS, SAMPLE_RATE, (ma_uint32)(SAMPLE_RATE * DELAY_IN_SECONDS), DECAY);
			ma_delay_node_init(&nodeGraph, &delayNodeConfig, NULL, &synth->delay);
			ma_node_attach_output_bus(&synth->delay, 0, &lowPass, 0);

			// Synths
			ma_waveform_config droneConfig = ma_waveform_config_init(FORMAT, CHANNELS, SAMPLE_RATE, ma_waveform_type_sine, 0.0, 220);
			ma_waveform_init(&droneConfig, &synth->drone);
			ma_data_source_node_config droneNodeConfig = ma_data_source_node_config_init(&synth->drone);
			ma_data_source_node_init(&nodeGraph, &droneNodeConfig, NULL, &synth->droneNode);
			ma_node_attach_output_bus(&synth->droneNode, 0, &synth->delay, 0);

			ma_waveform_config arpConfig = ma_waveform_config_init(ma_format_f32, CHANNELS, SAMPLE_RATE, ma_waveform_type_triangle, 0.0, 440);
			ma_waveform_init(&arpConfig, &synth->arp);
			ma_data_source_node_config arpNodeConfig = ma_data_source_node_config_init(&synth->arp);
			ma_data_source_node_init(&nodeGraph, &arpNodeConfig, NULL, &synth->arpNode);
			ma_node_attach_output_bus(&synth->arpNode, 0, &synth->delay, 0);
		}

        ma_device_config deviceConfig;
        deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.format   = FORMAT;
        deviceConfig.playback.channels = CHANNELS;
        deviceConfig.sampleRate        = SAMPLE_RATE;
        deviceConfig.dataCallback      = data_callback;
        deviceConfig.pUserData         = &nodeGraph;

		if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
			printf("Failed to open playback device.\n");
		}

		if (ma_device_start(&device) != MA_SUCCESS) {
			printf("Failed to start playback device.\n");
			ma_device_uninit(&device);
		}
	}

	~MainSoundComponent() {
        ma_device_uninit(&device);
	}

	static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
	{
		ma_node_graph_read_pcm_frames(&nodeGraph, pOutput, frameCount, NULL);
		for (auto synthIndex : arpsToPlay) {
			auto synth = synths[synthIndex].get();
			ma_waveform_set_amplitude(&synth->arp, 0.0);
		}
		arpsToPlay.clear();
		(void)pInput;   /* Unused. */
		(void)pDevice;  /* Unused. */
	}

	// A=440 Hz, B=493.88 Hz, C#=554.37 Hz, D=587.33 Hz, E=659.25 Hz, F#=739.99 Hz, and G#=830.61
	const std::vector<double> notes = { 440.0, 493.88, 554.37, 587.33, 659.25, 739.99, 830.61, 880.0 };

	void updateFrequency(int synthIndex, float input) {
		auto noteIdx = (int)(input * (notes.size() - 1));
		auto frequency = notes[noteIdx];
		auto synth = synths[synthIndex].get();
		ma_waveform_set_frequency(&synth->drone, frequency / 2.0);
		ma_waveform_set_frequency(&synth->arp, frequency);
	}
	void updateAmplitude(int synthIndex, float amplitude) {
		auto synth = synths[synthIndex].get();
		ma_waveform_set_amplitude(&synth->drone, (double)amplitude);
		ma_waveform_set_amplitude(&synth->arp, (double)amplitude);
	}
	void play(int synthIndex, SoundComponentTarget sound) { 
		auto synth = synths[synthIndex].get();
		if (sound == DRONE) {
			ma_waveform_set_amplitude(&synth->drone, 0.1);
		} else {
			arpsToPlay.push_back(synthIndex);
			ma_waveform_set_amplitude(&synth->arp, 0.2);
		}
	}
	void stop(int synthIndex) {
		auto synth = synths[synthIndex].get();
		ma_waveform_set_amplitude(&synth->drone, 0.0);
	}
	void updateDelay(int synthIndex, double value) {}
	void updateFeedback(int synthIndex, double value) {}
	void updateCutoff(double value) {}
};

class MuzakOfTheSpheres : public olc::PixelGameEngine
{
public:
    float totalElapsedTime = 0.f;
    Star sun;
    std::vector<std::unique_ptr<Planet>> planets;
    std::vector<std::unique_ptr<SolarFlare>> flares;
	std::vector<olc::vf2d> explosions;
	std::vector<Particle> vecParticles;

	bool mouseHeld = false;

	bool highlightingPlanet = false;
	Planet* highlightedPlanet = NULL;

	bool modifyingPlanet = false;
	Planet* modifiedPlanet = NULL;
	olc::vf2d clickOffset;

	Background background = Background();
	MainSoundComponent soundEngine = MainSoundComponent();

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
			if (flare->exceedsScreen()) {
				flares.erase(std::next(flares.begin(), idx));
				continue;
			}
            for (auto idx2 = 0; idx2 < planets.size(); idx2++) {
                auto body = planets[idx2].get();
				auto currentlyActivated = body->isActivated;
				auto inSolarFlare = isInSolarFlare(flare, body);
                if (inSolarFlare && !currentlyActivated) {
					body->isActivated = true;
					soundEngine.play(body->id, DRONE);
                } else if (!inSolarFlare && currentlyActivated) {
					body->isActivated = false;
					soundEngine.stop(body->id);
				}
            }
		}
	}

	bool isInSolarFlare(SolarFlare* flare, Body* body) {
		Body bloom = flare->getBloomShape();
		bool collided = areColliding(flare, body);
		bool notInsideBloom = !contains(&bloom, body);
		return collided && notInsideBloom;
	}

	void handleUserInput() {
        bool mouseDown = mouse.GetButton(0).bHeld;
		bool newClick = mouseDown && !mouseHeld;
		mouseHeld = mouseDown;
		olc::vf2d mousePosition = mouse.GetPosition().round();
		olc::vf2d adjustedMousePosition = { mousePosition.x - SCREENSIZE.x / 2.f, mousePosition.y - SCREENSIZE.y / 2.f };

		// User clicked on planet. Update planet to draw ellipse + play drone to preview sound.
		if (newClick && highlightingPlanet) {
			highlightingPlanet = false;
			modifyingPlanet = true;
			modifiedPlanet = highlightedPlanet;
			soundEngine.play(modifiedPlanet->id, DRONE);
			return;
		} 

		// User is modifying a planet. Update its frequency.
		if (mouseDown && modifyingPlanet) {
			modifiedPlanet->modifyOrbit(adjustedMousePosition, clickOffset);
			auto frequencyInput = (modifiedPlanet->orbitRadius * modifiedPlanet->orbitEccentricity) / (SCREENSIZE.x * SCREENSIZE.y / 4.f);
			soundEngine.updateFrequency(modifiedPlanet->id, frequencyInput);
			return;
		}

		// User stopped modifying a planet. Update planet to stop drawing ellipse + stop drone.
		if (!mouseDown && modifyingPlanet) {
			modifyingPlanet = false;
			soundEngine.stop(modifiedPlanet->id);
		}

		// User was highlighting a planet.
		if (highlightingPlanet) {
			// Make sure they still are + update state.
			for (auto& orbitPoint : highlightedPlanet->orbitOutline) {
				auto dist = distance(orbitPoint, adjustedMousePosition);
				if (dist < 20.f) {
					clickOffset = { orbitPoint.x - adjustedMousePosition.x, orbitPoint.y - adjustedMousePosition.y };
					return;
				}
			}
			// Else, update planet to stop drawing ellipse.
			highlightingPlanet = false;
			highlightedPlanet->isHighlighted = false;
		// User wasn't doing anything; check if a planet should be highlighted.
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
	}

	void drawState(float dt) {
		draw.WorldOffset(CENTER);
        draw.Clear(olc::Colour::BLACK);
		handleUserInput();

		// Simulate
		background.update();
        for (auto& flarePtr : flares) {
            auto flare = flarePtr.get();
            flare->update(dt);
        }
		sun.update(dt, totalElapsedTime);
		if (sun.shouldFlare) {
			flares.push_back(std::make_unique<SolarFlare>(sun.radius, sun.id, sun.flareSpeed, sun.flareBloom, sun.flareColor));
			sun.shouldFlare = false;
		}
        for (auto& bodyPtr : planets) {
            auto body = bodyPtr.get();
            body->update(dt);
        }
		// - Asteroids and planet generation

		// Collision detection
        checkForCollisions();
		checkForSolarFlareActivations();

		// Draw
		for (auto& backgroundStar : background.starfield) {
            draw.FilledCircle(backgroundStar.position, backgroundStar.activated ? 0.4f : 0.2f, olc::Colour::WHITE);
		}
        for (auto& flarePtr : flares) {
            auto flare = flarePtr.get();
            draw.FilledCircle(flare->position, flare->radius, flare->color);
			draw.FilledCircle(flare->position, flare->radius - flare->bloom, flare->bloomColor);
        }
        draw.FilledCircle({0.f, 0.f}, sun.radius, sun.color);
        for (auto& bodyPtr : planets) {
            auto body = bodyPtr.get();
            draw.FilledCircle(body->position, body->radius, olc::Colour::BLUE);
			if (body->isHighlighted) {
				for (auto& point : body->orbitOutline) {
					draw.FilledCircle(point, 0.5f, olc::Colour::WHITE);
				}
			}
        }
		// - Asteroids
		handleExplosions(dt);

		draw.WorldReset();
		if (sun.showingPopup) {
			olc::vf2d largeText = draw.GetTextSize(sun.epochName, false, { 2.0f, 4.0f });
			draw.String({ 5,5 }, sun.epochName, olc::Colour::WHITE, { 2.0f, 4.0f });
			draw.String({ 5,largeText.y + 5.f }, sun.epochYears, olc::Colour::WHITE, { 2.0f, 4.0f });
		} else if (sun.completedLifecycle) {
			olc::vf2d largeText = draw.GetTextSize("RESTART", false, { 2.0f, 4.0f });
			draw.String({ CENTER.x - largeText.x, SCREENSIZE.y - (largeText.y / 2.f) }, "RESTART", olc::Colour::WHITE, { 2.0f, 4.0f });
			// TODO: It doesn't do anything yet
		}

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
			soundEngine.play(body2->id, ARP);
			removeEntity(body2->id);
		} else if (body1->radius < body2->radius) {
			explosions.push_back(body1->position);
			soundEngine.play(body1->id, ARP);
			removeEntity(body1->id);
		} else {
			explosions.push_back(body1->position);
			soundEngine.play(body1->id, ARP);
			removeEntity(body1->id);
			explosions.push_back(body2->position);
			soundEngine.play(body2->id, ARP);
			removeEntity(body2->id);
		}
	}

	void removeEntity(int id) {
		if (highlightedPlanet != NULL && highlightedPlanet->id == id) { 
			highlightedPlanet = NULL;
			highlightingPlanet = false;
		}
		if (modifiedPlanet != NULL && modifiedPlanet->id == id) { 
			modifiedPlanet = NULL;
			modifyingPlanet = false; 
		}
		soundEngine.stop(id);
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
			float angle = randomFloat() * 2.0f * 3.14f;
            float speed = 5.0f + randomFloat();
            
            Particle p;
            p.position = pos;
            p.velocity = olc::vf2d{
				std::cos(angle) * speed,
				std::sin(angle) * speed
			};
            p.color = color;
            p.life = 1.0f + randomFloat();
            
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
