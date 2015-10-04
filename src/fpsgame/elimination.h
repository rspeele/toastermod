#ifndef PARSEMESSAGES
#ifdef SERVMODE
bool betweenrounds = false;
struct elimservmode : servmode
#else
struct elimclientmode : clientmode
#endif
{
    struct score
    {
        string team;
        int total;
    };

    vector<score> scores;

    score *lookupscore(const char *team)
    {
        loopv(scores)
        {
            if(!strcmp(scores[i].team, team)) return &scores[i];
        }
        return NULL;
    }
    score &makescore(const char *team)
    {
        score *sc = lookupscore(team);
        if(!sc)
        {
            score &add = scores.add();
            add.total = 0;
            copystring(add.team, team);
            return add;
        }
        else return *sc;
    }
    int getteamscore(const char *team)
    {
        score *sc = lookupscore(team);
        if(sc) return sc->total;
        else return 0;
    }
    void getteamscores(vector<teamscore> &teamscores)
    {
        loopv(scores) teamscores.add(teamscore(scores[i].team, scores[i].total));
    }

    void setup()
    {
#ifdef SERVMODE
        betweenrounds = false;
#endif
        scores.setsize(0);
    }
    void cleanup()
    {
    }
    bool hidefrags()
    {
        return true;
    }
#ifdef SERVMODE
    int pickspawn(clientinfo *ci)
    {
        return pickplayerspawn(ci);
    }
    void endround(const char *winner)
    {
        if(!winner) return;
        score &sc = makescore(winner);
        sc.total++;
        sendf(-1, 1, "ri2s", N_ROUNDSCORE, sc.total, sc.team);
        betweenrounds = true;
    }
    static void startround()
    {
        loopv(clients)
        {
            if(clients[i]->state.state!=CS_SPECTATOR)
            {
                clients[i]->state.reassign();
                sendspawn(clients[i]);
            }
        }
        betweenrounds = false;
    }
    bool checkround;
    struct winstate
    {
        bool over;
        const char *winner;
    };
    const winstate winningteam()
    {
        winstate won = { false, NULL };
        const char *aliveteam = NULL;
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->state.state==CS_ALIVE)
            {
                if(aliveteam)
                {
                    if(strcmp(aliveteam, ci->team)) return won;
                }
                else aliveteam = ci->team;
            }
        }
        won.over = true;
        won.winner = aliveteam;
        return won;
    }
    virtual void initclient(clientinfo *ci, packetbuf &p, bool connecting)
    {
        if(!connecting) return;
        loopv(scores)
        {
            score &sc = scores[i];
            putint(p, N_ROUNDSCORE);
            putint(p, sc.total);
            sendstring(sc.team, p);
        }
    }
    void entergame(clientinfo *ci)
    {
        checkround = true;
    }
    void leavegame(clientinfo *ci, bool disconnecting = false)
    {
        checkround = true;
    }
    void died(clientinfo *victim, clientinfo *actor)
    {
        checkround = true;
    }
    bool canspawn(clientinfo *ci, bool connecting)
    {
        const int numplayers = numclients(-1, true, false);
        return numplayers <= 1 || (numplayers <= 2 && connecting);
    }
    bool canchangeteam(clientinfo *ci, const char *oldteam, const char *newteam)
    {
        return true;
        // only allow two teams?
    }
    void update()
    {
        if(!checkround) return;
        checkround = false;
        if(betweenrounds) return;
        winstate won = winningteam();
        if(won.over)
        {
            endround(won.winner);
            sendbroadcastf("round: %s", 5000, won.winner ? won.winner : "draw");
            serverevents::add(&startround, 5000);
        }
    }
#else
    bool canfollow(const fpsent *spec, const fpsent *player)
    {
        if(spec->state != CS_SPECTATOR && (spec->state != CS_DEAD || player->state == CS_DEAD)) return false;
        if(isteam(spec->team, player->team)) return true;
        loopv(players)
        { // if any living players are on your team, you can't spec an opponent
            fpsent *o = players[i];
            if(o->state == CS_ALIVE && isteam(o->team, spec->team)) return false;
        }
        return true;
    }
#endif

};
#elif SERVMODE
#else
case N_ROUNDSCORE:
{
    int score = getint(p);
    getstring(text, p);
    if(p.overread() || !text[0]) break;
    eliminationmode.makescore(text).total = score;
    break;
}
#endif
