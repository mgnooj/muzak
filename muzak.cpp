#define OLC_PGE3_APPLICATION
#include "olcPixelGameEngine3.h"

#if defined(__PGETINKER__)
#include "pgetinker.h"
#else
static inline void pgetinker_file_resolve(const char* url, const char* mountPath) {}
#endif

const olc::vf2d screenSize = { 500.f,500.f };
const float MU = 12000.f;

enum starState {
    newborn, mature, redgiant, whitedwarf
};

struct star {
    const olc::vf2d position = { screenSize.x / 2.f, screenSize.y / 2.f };
    starState starState = newborn;
};

struct body {
    olc::vf2d position;
	olc::vf2d velocity;
    int radius;
	std::vector<olc::vf2d> path = {};
};

class MuzakOfTheSpheres : public olc::PixelGameEngine
{
public:
    MuzakOfTheSpheres()
    {
        sAppName = "Muzak of the Spheres";
    }
public:

    int year;
    star sun = star();
    std::vector<body> planets = {
            { .position = {150.f, 0.f}, .velocity = {0.f, 10.f}, .radius = 8 },
            { .position = {90.f, 0.f}, .velocity = {0.f, 12.f}, .radius = 5 },
        };
	std::vector<body> asteroids = {};

	bool OnUserCreate() override
	{
		return true;
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
        draw.Clear(olc::Colour::BLACK);

        draw.WorldReset();
		draw.WorldOffset(sun.position);

        for (body &planet : planets) {
            updateBody(planet, 0.4f);
            for (olc::vf2d prevPos : planet.path)
                draw.Circle(prevPos, 0.5f);
            draw.FilledCircle(planet.position, planet.radius, olc::Colour::BLUE);
        }

        draw.FilledCircle({0.f, 0.f}, 20.0f, olc::Colour::TANGERINE);

        return true;
    }

	// Physics formula mostly derived from
	// https://www.mysimulator.uk/content/tutorials/orbit-simulation-50-lines-js.html
	olc::vf2d accelerationVector(body &body) {
		const float r2 = body.position.x * body.position.x + body.position.y * body.position.y;
		const float r = std::sqrt(r2);
		const float f = -MU / (r2 * r);
		return { f * body.position.x, f * body.position.y };
	}

	void updateBody(body &body, float dt) {
		const auto a0 = accelerationVector(body);
		body.position.x += body.velocity.x * dt + 0.5f * a0.x * dt * dt;
		body.position.y += body.velocity.y * dt + 0.5f * a0.y * dt * dt;
		const auto a1 = accelerationVector(body);
		body.velocity.x += 0.5f * (a0.x + a1.x) * dt;
		body.velocity.y += 0.5f * (a0.y + a1.y) * dt;

		body.path.insert(body.path.begin(), body.position);
        if (body.path.size() > 200)
            body.path.pop_back();
	}

	
};

int main()
{
	MuzakOfTheSpheres game;

	olc::PGEConfig config;
	config.bVSync = false;
	config.vPixelSize = { 1,1 };
	config.vScreenSize = screenSize;

	if (game.Construct(config))
		game.Start();

	return 0;
}