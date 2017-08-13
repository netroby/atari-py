#include <ale_interface.hpp>
#include <vector>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static std::string env_id;
static std::string rom;
static std::string monitor_dir;
static std::string prefix;
static int LUMP;
static int cpu;
static int NCPU;
static int BUNCH;
static int STEPS;
static int SKIP;
static int STACK;
const int W = 80;
const int H = 105;
const int SMALL_PICTURE_BYTES =   H*W;
const int FULL_PICTURE_BYTES  = 210*160; // AKA 2H*2W

std::string stdprintf(const char* fmt, ...)
{
        char buf[32768];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        buf[32768-1] = 0;
        return buf;
}

double time()
{
	struct timespec time;
	clock_gettime(CLOCK_REALTIME, &time); // you need macOS Sierra 10.12 for this
	return time.tv_sec + 0.000000001*time.tv_nsec;
}

struct UsefulData {
	std::vector< std::vector<uint8_t> > picture_stack;
	int picture_rotation;
	int lives;
	int frame;
	int score;
};

template<class T>
class MemMap {
	int _len;
public:
	int fd;
	T* d;
	int chunk;

	MemMap(const std::string& fn, int size):
		_len(0),
		fd(-1),
		d(0)
	{
		fd = open(fn.c_str(), O_RDWR);
		if (fd==-1)
			throw std::runtime_error(stdprintf("cannot open file '%s': %s", fn.c_str(), strerror(errno)));
		_len = sizeof(T)*size;
		int file_on_disk_size = lseek(fd, 0, SEEK_END);
		if (file_on_disk_size != _len) {
			close(fd);
			throw std::runtime_error(stdprintf("file on disk '%s' has size %i, but expected size is %i",
				fn.c_str(), file_on_disk_size, _len) );
		}
		d = (T*) mmap(0, _len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		if (d==MAP_FAILED) {
			close(fd);
			throw std::runtime_error(stdprintf("cannot mmap '%s': %s", fn.c_str(), strerror(errno)));
		}
		assert( size % (LUMP*NCPU*BUNCH*STEPS) == 0 );
		chunk = size / (LUMP*NCPU*BUNCH*STEPS);
	}

	~MemMap()
	{
		if (d)
			munmap(d, _len);
		if (fd!=-1)
			close(fd);
	}

	T* at(int l, int b, int cursor)
	{
		return d + chunk*(l*NCPU*BUNCH*STEPS + cpu*BUNCH*STEPS + b*STEPS + cursor);
	}
	// shape_with_details    = [LUMP, NENV, STEPS]
	// NENV = NCPU*BUNCH
};

void main_loop()
{
	char buf[1024];
	snprintf(buf, sizeof(buf), "%i", cpu);
        FILE* monitor_js = fopen((monitor_dir + stdprintf("/%03i.monitor.json", cpu)).c_str(), "wt");
        double t0 = time();
        fprintf(monitor_js, "{\"t_start\": %0.2lf, \"gym_version\": \"vecgym\", \"env_id\": \"%s\"}\n", t0, env_id.c_str());
        fflush(monitor_js);

        MemMap<uint8_t> buf_obs0(prefix+"_obs0", LUMP*NCPU*BUNCH*STEPS*H*W*STACK);
        MemMap<int32_t> buf_acts(prefix+"_acts", LUMP*NCPU*BUNCH*STEPS);
        MemMap<float>   buf_rews(prefix+"_rews", LUMP*NCPU*BUNCH*STEPS);
        MemMap<bool>    buf_news(prefix+"_news", LUMP*NCPU*BUNCH*STEPS);
        MemMap<int32_t> buf_step(prefix+"_step", LUMP*NCPU*BUNCH*STEPS);
        MemMap<float>   buf_scor(prefix+"_scor", LUMP*NCPU*BUNCH*STEPS);

        MemMap<uint8_t> last_obs0(prefix+"_xlast_obs0", LUMP*NCPU*BUNCH*1*H*W*STACK);
        MemMap<float>   last_rews(prefix+"_xlast_rews", LUMP*NCPU*BUNCH*1);
        MemMap<bool>    last_news(prefix+"_xlast_news", LUMP*NCPU*BUNCH*1);
        MemMap<int32_t> last_step(prefix+"_xlast_step", LUMP*NCPU*BUNCH*1);
        MemMap<float>   last_scor(prefix+"_xlast_scor", LUMP*NCPU*BUNCH*1);

	std::vector<std::vector<ALEInterface*> > lumps;
	std::vector<std::vector<UsefulData> > lumps_useful;
	int cursor = 0;
	int me = cpu*BUNCH;
	for (int l=0; l<LUMP; l++) {
		std::vector<ALEInterface*> bunch;
		std::vector<UsefulData> bunch_useful;
		for (int b=0; b<BUNCH; b++) {
			ALEInterface* emu = new ALEInterface();
			emu->setInt("random_seed", cpu*1000 + b);
			emu->loadROM(rom);
			assert( FULL_PICTURE_BYTES == emu->getScreen().height() * emu->getScreen().width() );
			UsefulData d;
			d.frame = 0;
			d.score = 0;
			d.lives = emu->lives();
			bunch.push_back(emu);
			bunch_useful.push_back(d);
			fprintf(stderr, "AAAAAA\n");
		}
		lumps.push_back(bunch);
		lumps_useful.push_back(bunch_useful);
	}

	fclose(monitor_js);

	// open shared
	// open pipe

//                env.__stacked_pictures = [obs]*STACK
//                for s in range(STACK):
//                    vecenv.buf_l_obs[0][l,me+b,cursor,:,:,s] = env.__stacked_pictures[s]
//                vecenv.buf_l_vo[0][l,me+b,cursor] = 1 - env.__frame/limit
//                vecenv.buf_l_rews[l,me+b,cursor] = 0
//                vecenv.buf_l_news[l,me+b,cursor] = True

//        pipe.send("ready")
//        for l in range(LUMP): pipe.send(l)
//        cursor += 1
//        quit = False
//        while not quit:
//            last = cursor==STEPS
//            for l in range(LUMP):
//                bunch = lumps[l]
//                sync_check, cmd = pipe.recv()   # Wait for action
//                quit |= cmd=='Q'
//                if quit: break
//                if cmd=='0':
//                    cursor = 0
//                    assert l==0
//                elif cmd=='S':
//                    pass
//                else:
//                    assert 0, "Something strange came to me through pipe: '%s'" % cmd
//                assert cursor < STEPS or last
//                assert sync_check==l
//                for b in range(BUNCH):
//                    env = bunch[b]
//                    life_lost = False
//                    ale_action = env.unwrapped._action_set[vecenv.buf_l_acts[l,me+b,cursor-1]]
//                    done = False
//                    rew  = 0
//                    raw_pictures = []
//                    for s in range(SKIP):
//                        r = env.unwrapped.ale.act(ale_action)
//                        rew += r
//                        env.__reward += r
//                        done |= env.unwrapped.ale.game_over()
//                        if done: break
//                        if s >= SKIP-2:
//                            raw_pictures.append( env.unwrapped.ale.getScreenRGB2() )
//                    reset_me = done

//                    if not reset_me:
//                        assert len(raw_pictures)==2
//                        picture = np.dot(np.maximum(
//                            resize_05x(raw_pictures[0]),
//                            resize_05x(raw_pictures[1]),
//                            ).astype('float32'), make_grayscale).astype('uint8')
//                        env.__stacked_pictures.append( picture )
//                        env.__stacked_pictures.pop(0)
//                        if not last:
//                            for s in range(STACK):
//                                vecenv.buf_l_obs[0][l,me+b,cursor,:,:,s] = env.__stacked_pictures[s]
//                        else:
//                            for s in range(STACK):
//                                vecenv.buf_obs_last[0][l,me+b,0,:,:,s] = env.__stacked_pictures[s]

//                    env.__frame += SKIP
//                    lives = env.unwrapped.ale.lives()
//                    if lives < env.__lives and lives > 0:
//                        done = True
//                    if lives < env.__lives:
//                        life_lost = True
//                    env.__lives = lives

//                    if env.__frame >= limit:
//                        reset_me = True

//                    if not last:
//                        vecenv.buf_l_rews[l,me+b,cursor] = rew
//                        vecenv.buf_l_news[l,me+b,cursor] = done
//                        vecenv.buf_l_scor[l,me+b,cursor] = env.__reward
//                        vecenv.buf_l_step[l,me+b,cursor] = env.__frame
//                        vecenv.buf_l_vo[0][l,me+b,cursor] = 1 - env.__frame/limit
//                    else:
//                        vecenv.buf_rews_last[l,me+b,0] = rew
//                        vecenv.buf_scor_last[l,me+b,0] = env.__reward
//                        vecenv.buf_step_last[l,me+b,0] = env.__frame
//                        vecenv.buf_vo_last[0][l,me+b,0] = 1 - env.__frame/limit

//                    #if cpu==0 and b==0 and l==0:
//                    #    sys.stdout.write('.' if not done else 'd')
//                    #    sys.stdout.flush()
//                    #    print("%s frame %06i/%06i lives %i act %i total rew %06.0f done %i" % (id(env.unwrapped),
//                    #        env.__frame, limit, lives, ale_action,
//                    #        env.__reward, done))
//                    if not reset_me: continue
//                    monitor_js.write(json.dumps( {"r": env.__reward, "l": env.__frame, "t": time.time() - vecenv.t0} )+"\n")
//                    monitor_js.flush()
//                    env.__frame  = 0
//                    env.__reward = 0
//                    env.__lives = 0
//                    env.__was_real_done  = False
//                    obs = np.dot(resize_05x( env.reset() ).astype('float32'), make_grayscale).astype('uint8')
//                    env.__stacked_pictures = [obs]*STACK
//                    if not last:
//                        for s in range(STACK):
//                            vecenv.buf_l_obs[0][l,me+b,cursor,:,:,s] = env.__stacked_pictures[s]
//                        assert vecenv.buf_l_news[l,me+b,cursor]==True
//                    else:
//                        for s in range(STACK):
//                            vecenv.buf_obs_last[0][l,me+b,0,:,:,s] = env.__stacked_pictures[s]
//                    # But keep the rewards, step, score

//                pipe.send(l)
//            cursor += 1
//    finally:
//        pipe.send(None)
//        pipe.close()
}

int main(int argc, char** argv)
{
	fprintf(stderr, "\n\n*************************************** ALE VECGYM **************************************\n");
	if (argc < 12) {
		fprintf(stderr, "I need more command line arguments!\n");
		return 1;
	}
	prefix      = argv[1];
	env_id      = argv[2];
	rom         = argv[3];
	monitor_dir = argv[4];
	LUMP    = atoi(argv[5]);
	cpu     = atoi(argv[6]);
	NCPU    = atoi(argv[7]);
	BUNCH   = atoi(argv[8]);
	STEPS   = atoi(argv[9]);
	SKIP    = atoi(argv[10]);
	STACK   = atoi(argv[11]);

	fprintf(stderr, "C++ LUMP=%i cpu=%i/CPU=%i BUNCH=%i STEPS=%i SKIP=%i STACK=%i\n",
		LUMP,
		cpu,
		NCPU,
		BUNCH,
		STEPS,
		SKIP,
		STACK);
	fprintf(stderr, "     prefix: %s\n", prefix.c_str());
	fprintf(stderr, "monitor dir: %s\n", monitor_dir.c_str());
	fprintf(stderr, "        rom: %s\n", rom.c_str());
	main_loop();
	return 0;
}
