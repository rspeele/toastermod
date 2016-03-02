#include "game.h"

namespace game
{
    bool intermission = false;
    int maptime = 0, maprealtime = 0, maplimit = -1;
    int respawnent = -1;
    int lasthit = 0, lastspawnattempt = 0;

    int following = -1, followdir = 0;

    fpsent *player1 = NULL;         // our client
    vector<fpsent *> players;       // other clients
    int savedammo[NUMGUNS];

    bool clientoption(const char *arg) { return false; }

    void taunt()
    {
        if(player1->state!=CS_ALIVE || player1->physstate<PHYS_SLOPE) return;
        if(lastmillis-player1->lasttaunt<1000) return;
        player1->lasttaunt = lastmillis;
        addmsg(N_TAUNT, "rc", player1);
    }
    COMMAND(taunt, "");

    ICOMMAND(getfollow, "", (),
    {
        fpsent *f = followingplayer();
        intret(f ? f->clientnum : -1);
    });

    bool spectating(physent *d)
    {
        return d->state==CS_SPECTATOR || (m_elimination && d->state==CS_DEAD);
    }

    const bool canfollow(const fpsent *const spec, const fpsent *const player)
    {
        return spec && player &&
            player->state != CS_SPECTATOR &&
            ((!cmode && spec->state == CS_SPECTATOR) ||
             (cmode && cmode->canfollow(spec, player)));
    }

	void follow(char *arg)
    {
        if(arg[0] ? spectating(player1) : following>=0)
        {
            following = arg[0] ? parseplayer(arg) : -1;
            if(following==player1->clientnum) following = -1;
            followdir = 0;
            conoutf("follow %s", following>=0 ? "on" : "off");
        }
	}
    COMMAND(follow, "s");

    void nextfollow(int dir)
    {
        if(clients.empty());
        else if(spectating(player1))
        {
            int cur = following >= 0 ? following : (dir < 0 ? clients.length() - 1 : 0);
            loopv(clients)
            {
                cur = (cur + dir + clients.length()) % clients.length();
                if(clients[cur] && canfollow(player1, clients[cur]))
                {
                    if(following<0) conoutf("follow on");
                    following = cur;
                    followdir = dir;
                    return;
                }
            }
        }
        stopfollowing();
    }
    ICOMMAND(nextfollow, "i", (int *dir), nextfollow(*dir < 0 ? -1 : 1));


    const char *getclientmap() { return clientmap; }

    void resetgamestate()
    {
        clearprojectiles();
        clearbouncers();
    }

    fpsent *spawnstate(fpsent *d)              // reset player state not persistent accross spawns
    {
        d->respawn();
        d->spawnstate(gamemode);
        return d;
    }

    void respawnself()
    {
        if(ispaused()) return;
        if(m_mp(gamemode))
        {
            int seq = (player1->lifesequence<<16)|((lastmillis/1000)&0xFFFF);
            if(player1->respawned!=seq) { addmsg(N_TRYSPAWN, "rc", player1); player1->respawned = seq; }
        }
        else
        {
            spawnplayer(player1);
            showscores(false);
            lasthit = 0;
            if(cmode) cmode->respawned(player1);
        }
    }

    fpsent *pointatplayer()
    {
        loopv(players) if(players[i] != player1 && intersect(players[i], player1->o, worldpos)) return players[i];
        return NULL;
    }

    void stopfollowing()
    {
        if(following<0) return;
        following = -1;
        followdir = 0;
        conoutf("follow off");
    }

    fpsent *followingplayer()
    {
        if((player1->state!=CS_SPECTATOR&&player1->state!=CS_DEAD) || following<0) return NULL;
        fpsent *target = getclient(following);
        if(target && target->state!=CS_SPECTATOR) return target;
        return NULL;
    }

    fpsent *hudplayer()
    {
        if(thirdperson) return player1;
        fpsent *target = followingplayer();
        return target ? target : player1;
    }

    void setupcamera()
    {
        fpsent *target = followingplayer();
        if(target)
        {
            player1->yaw = target->yaw;
            player1->pitch = target->state==CS_DEAD ? 0 : target->pitch;
            player1->o = target->o;
            player1->resetinterp();
        }
    }

    bool detachcamera()
    {
        fpsent *d = hudplayer();
        return d->state==CS_DEAD;
    }

    bool collidecamera()
    {
        switch(player1->state)
        {
            case CS_EDITING: return false;
            case CS_SPECTATOR: return followingplayer()!=NULL;
        }
        return true;
    }

    VARP(smoothmove, 0, 75, 100);
    VARP(smoothdist, 0, 32, 64);

    void predictplayer(fpsent *d, bool move)
    {
        d->o = d->newpos;
        d->yaw = d->newyaw;
        d->pitch = d->newpitch;
        d->roll = d->newroll;
        if(move)
        {
            moveplayer(d, 1, false);
            d->newpos = d->o;
        }
        float k = 1.0f - float(lastmillis - d->smoothmillis)/smoothmove;
        if(k>0)
        {
            d->o.add(vec(d->deltapos).mul(k));
            d->yaw += d->deltayaw*k;
            if(d->yaw<0) d->yaw += 360;
            else if(d->yaw>=360) d->yaw -= 360;
            d->pitch += d->deltapitch*k;
            d->roll += d->deltaroll*k;
        }
    }

    void otherplayers(int curtime)
    {
        loopv(players)
        {
            fpsent *d = players[i];
            if(d == player1 || d->ai) continue;

            if(d->state==CS_DEAD && d->ragdoll) moveragdoll(d);
            else if(!intermission)
            {
                if(lastmillis - d->lastaction >= d->gunwait) d->gunwait = 0;
                entities::checkpowerup(curtime, d);
            }

            const int lagtime = totalmillis-d->lastupdate;
            if(!lagtime || intermission) continue;
            else if(lagtime>1000 && d->state==CS_ALIVE)
            {
                d->state = CS_LAGGED;
                continue;
            }
            if(d->state==CS_ALIVE || d->state==CS_EDITING)
            {
                if(smoothmove && d->smoothmillis>0) predictplayer(d, true);
                else moveplayer(d, 1, false);
            }
            else if(d->state==CS_DEAD && !d->ragdoll && lastmillis-d->lastpain<2000) moveplayer(d, 1, true);
        }
    }

    VARFP(slowmosp, 0, 0, 1, { if(m_sp && !slowmosp) server::forcegamespeed(100); }); 

    void checkslowmo()
    {
        static int lastslowmohealth = 0;
        server::forcegamespeed(intermission ? 100 : clamp(player1->health, 25, 200));
        if(player1->health<player1->maxhealth && lastmillis-max(maptime, lastslowmohealth)>player1->health*player1->health/2)
        {
            lastslowmohealth = lastmillis;
            player1->health++;
        }
    }

    void updateworld()        // main game update loop
    {
        if(!maptime) { maptime = lastmillis; maprealtime = totalmillis; return; }
        if(!curtime) { gets2c(); if(player1->clientnum>=0) c2sinfo(); return; }

        physicsframe();
        ai::navigate();
        if(player1->state != CS_DEAD && !intermission)
        {
            entities::checkpowerup(curtime, player1);
        }
        updateweapons(curtime);
        otherplayers(curtime);
        ai::update();
        moveragdolls();
        gets2c();
        if(connected)
        {
            if(player1->state == CS_DEAD)
            {
                if(player1->ragdoll) moveragdoll(player1);
                else if(lastmillis-player1->lastpain<2000)
                {
                    player1->fmove = player1->fstrafe = 0.0f;
                    moveplayer(player1, 10, true);
                }
            }
            else if(!intermission)
            {
                if(player1->ragdoll) cleanragdoll(player1);
                moveplayer(player1, 10, true);
                swayhudgun(curtime);
                entities::checkitems(player1);
                if(m_sp)
                {
                    if(slowmosp) checkslowmo();
                    if(m_classicsp) entities::checktriggers();
                }
                else if(cmode) cmode->checkitems(player1);
            }
        }
        if(player1->clientnum>=0) c2sinfo();   // do this last, to reduce the effective frame lag
    }

    ICOMMAND(arena, "i", (int *a), { player1->arena = *a; });
    void spawnplayer(fpsent *d)   // spawn state
    {
        findplayerspawn(d, -1);
        spawnstate(d);
        if(d==player1)
        {
            if(editmode) d->state = CS_EDITING;
            else if(d->state != CS_SPECTATOR) d->state = CS_ALIVE;
        }
        else d->state = CS_ALIVE;
    }

    VARP(spawnwait, 0, 0, 1000);

    void respawn()
    {
        if(player1->state==CS_DEAD)
        {
            player1->attacking = false;
            int wait = cmode ? cmode->respawnwait(player1) : 0;
            if(wait>0)
            {
                lastspawnattempt = lastmillis;
                //conoutf(CON_GAMEINFO, "\f2you must wait %d second%s before respawn!", wait, wait!=1 ? "s" : "");
                return;
            }
            if(lastmillis < player1->lastpain + spawnwait) return;
            respawnself();
        }
    }

    // inputs

    void doattack(bool on)
    {
        if(!connected || intermission) return;
        if((player1->attacking = on)) respawn();
    }

    bool canjump()
    {
        if(!intermission) respawn();
        return player1->state!=CS_DEAD && !intermission;
    }

    bool cancrouch()
    {
        return false; //player1->state!=CS_DEAD && !intermission;
    }

    bool allowmove(physent *d)
    {
        if(d->type!=ENT_PLAYER) return true;
        return !((fpsent *)d)->lasttaunt || lastmillis-((fpsent *)d)->lasttaunt>=1000;
    }

    // broadcast messages from server
    char broadcastmsg[MAXTRANS];
    int broadcastexpire = 0;
    void showbroadcast(const char *message, int duration)
    {
        copystring(broadcastmsg, message, MAXTRANS);
        broadcastexpire = lastmillis + duration;
    }
    const char *currentbroadcast()
    {
        static const char *nothing = "";
        static string buffer;
        if(lastmillis >= broadcastexpire) return nothing;
        formatstring(buffer, broadcastmsg, (broadcastexpire - lastmillis + 900) / 1000); // the +900 ensures that 0 seconds will show for 100ms
        return buffer;
    }

    // custom HUD
    namespace hudstate
    {
        void load(const char *name);
    }
    SVARFP(customhud, "", hudstate::load(customhud));
    namespace hudstate
    {
        uint lastscrw = -1, lastscrh = -1;
        uint scrw = 0, scrh = 0;
        float aspect;
        uint r = 0xff, g = 0xff, b = 0xff, a = 0xff;
        int x = 0, y = 0, w = 50, h = 0;
        char xa = 'l', ya = 't';
        float scale = 1.0f;
        uint *code = NULL;

        void setxy(int tx, int ty)
        {
            x = tx;
            y = ty;
        }
        void setcolor(int rgb, int ta)
        {
            r = rgb >> 16 & 0xff;
            g = rgb >> 8  & 0xff;
            b = rgb       & 0xff;
            a = ta ? ta : 0xff;
//            glColor4ub(r, g, b, a);
        }
        void setalpha(int set)
        {
            a = set;
//            glColor4ub(r, g, b, a);
        }
        void align(int &tx, int &ty, int tw, int th)
        {
            tx -= xa == 'r' ? tw : (xa == 'c' ? tw/2 : 0);
            ty -= ya == 'b' ? th : (ya == 'c' ? th/2 : 0);
        }
        void image(const char *img)
        {
            int ax = x, ay = y;
            align(ax, ay, w, h);
            settexture(img);
            gle::defvertex(2);
            gle::deftexcoord0();
            gle::color(bvec(r, g, b), a);
            gle::begin(GL_TRIANGLE_STRIP);
            gle::attribf(ax,   ay);   gle::attribf(0.0f, 0.0f);
            gle::attribf(ax+w, ay);   gle::attribf(1.0f, 0.0f);
            gle::attribf(ax,   ay+h); gle::attribf(0.0f, 1.0f);
            gle::attribf(ax+w, ay+h); gle::attribf(1.0f, 1.0f);
            gle::end();
        }
        void rectangle()
        {
            int ax = x, ay = y;
            align(ax, ay, w, h);
            enabletexture(false);
            gle::defvertex(2);
            gle::color(bvec(r, g, b), a);
            gle::begin(GL_TRIANGLE_STRIP);
            gle::attribf(ax, ay);
            gle::attribf(ax + w, ay);
            gle::attribf(ax, ay + h);
            gle::attribf(ax + w, ay + h);
            gle::end();
            enabletexture(true);
        }
        void text(char *str)
        {
            int ax = x, ay = y, aw, ah;
            text_bounds(str, aw, ah);
            aw *= scale; ah *= scale;
            align(ax, ay, aw, ah);
            ax /= scale; ay /= scale;
            pushhudmatrix();
            hudmatrix.scale(scale, scale, 1.0f);
            flushhudmatrix();
            draw_text(str, ax, ay, r, g, b, a);
            pophudmatrix();
        }
        void textheight(int h)
        {
            int test, trash;
            text_bounds("0", trash, test);
            scale = (float)h / (float)test;
        }
        void load(const char *name)
        {
            if(!*name)
            {
                if(code) delete[] code;
                code = NULL;
                return;
            }
            string huddir = "packages/huds/";
            concatstring(huddir, name);
            execfile(makerelpath(huddir, "init.cfg"), false);
            char *buf = loadfile(makerelpath(huddir, "hud.cfg"), NULL);
            if (!buf)
            {
                conoutf(CON_ERROR, "missing script for hud \"%s\"", name);
                return;
            }
            if (code) delete[] code;
            code = compilecode(buf);
            delete[] buf;
        }
        void draw()
        {
            if (!(scrw && scrh)) return;
            if (scrw != lastscrw || scrh != lastscrh)
            {
                aspect = (float)scrw / (float)scrh;
                load(customhud);
            }
            if(!code) return;
            pushhudmatrix();
            hudmatrix.translate(scrw / 2, scrh / 2, 0);
            float scale = scrh * 1.0f/2000.0f;
            hudmatrix.scale(scale, scale, 1);
            flushhudmatrix();
            execute(code);
            pophudmatrix();
            setfont("default");
            lastscrw = scrw;
            lastscrh = scrh;
        }
    }

    // rendering HUD elements and adjusting state
    ICOMMAND(hudcolor, "ii", (int *rgb, int *a), hudstate::setcolor(*rgb, *a));
    ICOMMAND(hudalpha, "i", (int *a), hudstate::setalpha(*a));
    ICOMMAND(hudpos, "ii", (int *x, int *y), hudstate::setxy(*x, *y));
    ICOMMAND(hudsize, "ii", (int *w, int *h), {hudstate::w = *w; hudstate::h = *h;});
    ICOMMAND(hudtextscale, "f", (float *s), {hudstate::scale = *s;});
    ICOMMAND(hudtextheight, "i", (int *h), hudstate::textheight(*h));
    ICOMMAND(hudalign, "ss", (char *ya, char *xa), {hudstate::xa = *xa | 32; hudstate::ya = *ya | 32;});
    ICOMMAND(hudfont, "s", (char *font), setfont(font));
    ICOMMAND(hudimage, "s", (char *img), hudstate::image(img));
    ICOMMAND(hudrectangle, "", (), hudstate::rectangle());
    ICOMMAND(hudtext, "s", (char *str), hudstate::text(str));
    // getting info for hud
    ICOMMANDN(hud:aspect, hudp_aspect, "", (), floatret(hudstate::aspect));
    ICOMMANDN(hud:left, hudp_left, "i", (int *x), intret(-1000.0f * hudstate::aspect + *x));
    ICOMMANDN(hud:right, hudp_right, "i", (int *x), intret(1000.0f * hudstate::aspect - *x));
    ICOMMANDN(hud:health, hudp_health, "", (), intret(hudplayer()->health));
    ICOMMANDN(hud:armour, hudp_armour,  "", (), intret(hudplayer()->armour));
    ICOMMANDN(hud:armourtype, hudp_armourtype, "", (), intret(hudplayer()->armourtype));
    ICOMMANDN(hud:survivable, hudp_survivable, "", (), intret(hudplayer()->survivable()));
    ICOMMANDN(hud:ammo, hudp_ammo, "i", (int *i), if(validgun(*i)) intret(hudplayer()->ammo[*i]));
    ICOMMANDN(hud:magazine, hudp_magazine, "i", (int *i), if(validgun(*i)) intret(hudplayer()->magazine[*i]));
    ICOMMANDN(hud:capacity, hudp_capacity, "i", (int *i), if(validgun(*i)) intret(guns[*i].capacity));
    ICOMMANDN(hud:maxammo, hudp_maxammo, "i", (int *i), if(*i >= GUN_SG && *i <= GUN_PISTOL) intret(itemstats[*i - GUN_SG].max));
    ICOMMANDN(hud:gun, hudp_gun, "", (), intret(hudplayer()->gunselect));
    ICOMMANDN(hud:speed, hudp_speed, "", (), intret(hudplayer()->vel.magnitude2()));
    ICOMMANDN(hud:move, hudp_move, "", (), intret(hudplayer()->fmove < 0.0f ? -1 : hudplayer()->fmove > 0.0f ? 1 : 0));
    ICOMMANDN(hud:strafe, hudp_strafe, "", (), intret(hudplayer()->fstrafe < 0.0f ? -1 : hudplayer()->fstrafe > 0.0f ? 1 : 0));
    ICOMMANDN(hud:jumping, hudp_jumping, "", (), intret(hudplayer()->jumping));
    ICOMMANDN(hud:attacking, hudp_attacking, "", (), intret(hudplayer()->attacking));
    ICOMMANDN(hud:broadcast, hudp_broadcast, "", (), result(currentbroadcast()));
    VARP(showkeys, 0, 0, 1);
    // clock
    VARP(clockup, 0, 0, 1);
    int clockmillis() { return max(clockup ? lastmillis - maptime : maplimit - lastmillis, 0); }
    ICOMMAND(clocksecs, "", (), intret(clockmillis()/1000));
    ICOMMAND(clock, "i", (int *ts), {
            int mins = *ts/60;
            int secs = *ts%60;
            defformatstring(sn, "%02d:%02d", mins, secs);
            result(sn);
        });

    VARP(hitsound, 0, 0, 1);

    void damaged(int damage, fpsent *d, fpsent *actor, bool local)
    {
        if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;

        if(local) damage = d->dodamage(damage);

        fpsent *h = hudplayer();
        if(actor==h && d!=actor)
        {
            if(hitsound && lasthit != lastmillis) playsound(S_HIT);
            lasthit = lastmillis;
        }
        if(d==h)
        {
            damageblend(damage);
            damagecompass(damage, actor->o);
        }
        else if(actor==h) damageeffect(damage, d, d!=h);

		ai::damaged(d, actor);

        if(m_sp && slowmosp && d==player1 && d->health < 1) d->health = 1;

        if(d->health<=0) { if(local) killed(d, actor); }
        else if (lastmillis - d->lastyelp >= 800)
        {
            if(d==h) playsound(S_PAIN6);
            else playsound(S_PAIN1+rnd(5), &d->o);
            d->lastyelp = lastmillis;
            d->lastpain = lastmillis;
        }
    }

    VARP(deathscore, 0, 1, 1);

    void deathstate(fpsent *d, bool restore)
    {
        d->state = CS_DEAD;
        d->lastpain = lastmillis;
        if(!restore) gibeffect(max(-d->health, 0), d->vel, d);
        if(d==player1)
        {
            if(deathscore) showscores(true);
            disablezoom();
            if(!restore) loopi(NUMGUNS) savedammo[i] = player1->ammo[i];
            d->attacking = false;
            if(!restore) d->deaths++;
            //d->pitch = 0;
            d->roll = 0;
            playsound(S_DIE1+rnd(2));
        }
        else
        {
            d->fmove = d->fstrafe = 0.0f;
            d->resetinterp();
            d->smoothmillis = 0;
            playsound(S_DIE1+rnd(2), &d->o);
        }
    }

    VARP(teamcolorfrags, 0, 1, 1);

    void killed(fpsent *d, fpsent *actor, int gun)
    {
        if(d->state==CS_EDITING)
        {
            d->editstate = CS_DEAD;
            if(d==player1) d->deaths++;
            else d->resetinterp();
            return;
        }
        else if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;

        fpsent *h = followingplayer();
        if(!h) h = player1;
        int contype = d==h || actor==h ? CON_FRAG_SELF : CON_FRAG_OTHER;
        const char *dname = "", *aname = "";
        if(m_teammode && teamcolorfrags)
        {
            dname = teamcolorname(d, "you");
            aname = teamcolorname(actor, "you");
        }
        else
        {
            dname = colorname(d, NULL, "", "", "you");
            aname = colorname(actor, NULL, "", "", "you");
        }
        const char *with = "", *gname = "";
        if (gun > 0 && gun < NUMGUNS)
        {
            with = " with ";
            gname = guns[gun].name;
        }
        if(d==actor)
            conoutf(contype, "\f2%s suicided%s", dname, d==player1 ? "!" : "");
        else if(isteam(d->team, actor->team))
        {
            contype |= CON_TEAMKILL;
            if(actor==player1) conoutf(contype, "\f6%s fragged a teammate (%s)%s%s", aname, dname, with, gname);
            else if(d==player1) conoutf(contype, "\f6%s got fragged by a teammate (%s)%s%s", dname, aname, with, gname);
            else conoutf(contype, "\f2%s fragged a teammate (%s)%s%s", aname, dname, with, gname);
        }
        else
        {
            if(d==player1) conoutf(contype, "\f2%s got fragged by %s @ %dhp%s%s", dname, aname, actor->health, with, gname);
            else conoutf(contype, "\f2%s fragged %s%s%s", aname, dname, with, gname);
        }
        deathstate(d);
		ai::killed(d, actor);
    }

    void timeupdate(int secs)
    {
        if(secs > 0)
        {
            maplimit = lastmillis + secs*1000;
        }
        else
        {
            intermission = true;
            player1->attacking = false;
            if(cmode) cmode->gameover();
            conoutf(CON_GAMEINFO, "\f2intermission:");
            conoutf(CON_GAMEINFO, "\f2game has ended!");
            if(m_ctf) conoutf(CON_GAMEINFO, "\f2player frags: %d, flags: %d, deaths: %d", player1->frags, player1->flags, player1->deaths);
            else if(m_collect) conoutf(CON_GAMEINFO, "\f2player frags: %d, skulls: %d, deaths: %d", player1->frags, player1->flags, player1->deaths);
            else conoutf(CON_GAMEINFO, "\f2player frags: %d, deaths: %d", player1->frags, player1->deaths);
            int accuracy = (player1->totaldamage*100)/max(player1->totalshots, 1);
            conoutf(CON_GAMEINFO, "\f2player total damage dealt: %d, damage wasted: %d, accuracy(%%): %d", player1->totaldamage, player1->totalshots-player1->totaldamage, accuracy);

            showscores(true);
            disablezoom();

            if(identexists("intermission")) execute("intermission");
        }
    }

    ICOMMAND(getfrags, "", (), intret(player1->frags));
    ICOMMAND(getflags, "", (), intret(player1->flags));
    ICOMMAND(getdeaths, "", (), intret(player1->deaths));
    ICOMMAND(getaccuracy, "", (), intret((player1->totaldamage*100)/max(player1->totalshots, 1)));
    ICOMMAND(gettotaldamage, "", (), intret(player1->totaldamage));
    ICOMMAND(gettotalshots, "", (), intret(player1->totalshots));

    vector<fpsent *> clients;

    fpsent *newclient(int cn)   // ensure valid entity
    {
        if(cn < 0 || cn > max(0xFF, MAXCLIENTS + MAXBOTS))
        {
            neterr("clientnum", false);
            return NULL;
        }

        if(cn == player1->clientnum) return player1;

        while(cn >= clients.length()) clients.add(NULL);
        if(!clients[cn])
        {
            fpsent *d = new fpsent;
            d->clientnum = cn;
            clients[cn] = d;
            players.add(d);
        }
        return clients[cn];
    }

    fpsent *getclient(int cn)   // ensure valid entity
    {
        if(cn == player1->clientnum) return player1;
        return clients.inrange(cn) ? clients[cn] : NULL;
    }

    void clientdisconnected(int cn, bool notify)
    {
        if(!clients.inrange(cn)) return;
        if(following==cn)
        {
            if(followdir) nextfollow(followdir);
            else stopfollowing();
        }
        unignore(cn);
        fpsent *d = clients[cn];
        if(!d) return;
        if(notify && d->name[0]) conoutf("\f4leave:\f7 %s", colorname(d));
        removeweapons(d);
        removetrackedparticles(d);
        removetrackeddynlights(d);
        if(cmode) cmode->removeplayer(d);
        players.removeobj(d);
        DELETEP(clients[cn]);
        cleardynentcache();
    }

    void clearclients(bool notify)
    {
        loopv(clients) if(clients[i]) clientdisconnected(i, notify);
    }

    void initclient()
    {
        player1 = spawnstate(new fpsent);
        filtertext(player1->name, "unnamed", false, false, MAXNAMELEN);
        players.add(player1);
    }

    VARP(showmodeinfo, 0, 1, 1);

    void resetplayers()
    {
        // reset perma-state
        loopv(players)
        {
            fpsent *d = players[i];
            d->frags = d->flags = 0;
            d->deaths = 0;
            d->totaldamage = 0;
            d->totalshots = 0;
            d->maxhealth = 100;
            d->lifesequence = -1;
            d->respawned = d->suicided = -2;
        }
    }

    void restartgame()
    {
        clearprojectiles();
        clearbouncers();
        clearragdolls();

        resetteaminfo();
        resetplayers();

        setclientmode();

        intermission = false;
        maptime = maprealtime = 0;
        maplimit = -1;

        if(cmode) cmode->setup();

        showscores(false);
        disablezoom();
        lasthit = 0;
    }

    void startgame()
    {
        clearprojectiles();
        clearbouncers();
        clearragdolls();

        clearteaminfo();
        resetplayers();

        setclientmode();

        intermission = false;
        maptime = maprealtime = 0;
        maplimit = -1;

        if(cmode)
        {
            cmode->preload();
            cmode->setup();
        }

        conoutf(CON_GAMEINFO, "\f2game mode is %s", server::modename(gamemode));

        if(m_sp)
        {
            defformatstring(scorename, "bestscore_%s", getclientmap());
            const char *best = getalias(scorename);
            if(*best) conoutf(CON_GAMEINFO, "\f2try to beat your best score so far: %s", best);
        }
        else
        {
            const char *info = m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
            if(showmodeinfo && info) conoutf(CON_GAMEINFO, "\f0%s", info);
        }

        if(player1->playermodel != playermodel) switchplayermodel(playermodel);

        showscores(false);
        disablezoom();
        lasthit = 0;

        if(identexists("mapstart")) execute("mapstart");
    }
    COMMAND(startgame, "");

    void loadingmap(const char *name)
    {
        if(identexists("playsong")) execute("playsong");
    }

    void startmap(const char *name)   // called just after a map load
    {
        ai::savewaypoints();
        ai::clearwaypoints(true);

        respawnent = -1; // so we don't respawn at an old spot
        if(!m_mp(gamemode)) spawnplayer(player1);
        else findplayerspawn(player1, -1);
        entities::resetspawns();
        copystring(clientmap, name ? name : "");

        sendmapinfo();
    }

    const char *getmapinfo()
    {
        return showmodeinfo && m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
    }

    void physicstrigger(physent *d, bool local, int floorlevel, int waterlevel, int material)
    {
        if(d->type==ENT_INANIMATE) return;
        if     (waterlevel>0) { if(material!=MAT_LAVA) playsound(S_SPLASH1, d==player1 ? NULL : &d->o); }
        else if(waterlevel<0) playsound(material==MAT_LAVA ? S_BURN : S_SPLASH2, d==player1 ? NULL : &d->o);
        if     (floorlevel>0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) msgsound(S_JUMP, d); }
        else if(floorlevel<0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) msgsound(S_LAND, d); }
    }

    void dynentcollide(physent *d, physent *o, const vec &dir)
    {
    }

    void msgsound(int n, physent *d)
    {
        if(!d || d==player1)
        {
            addmsg(N_SOUND, "ci", d, n);
            playsound(n);
        }
        else
        {
            if(d->type==ENT_PLAYER && ((fpsent *)d)->ai)
                addmsg(N_SOUND, "ci", d, n);
            playsound(n, &d->o);
        }
    }

    int numdynents() { return players.length(); }

    dynent *iterdynents(int i)
    {
        if(i<players.length()) return players[i];
        return NULL;
    }

    bool duplicatename(fpsent *d, const char *name = NULL, const char *alt = NULL)
    {
        if(!name) name = d->name;
        if(alt && d != player1 && !strcmp(name, alt)) return true;
        loopv(players) if(d!=players[i] && !strcmp(name, players[i]->name)) return true;
        return false;
    }

    static string cname[3];
    static int cidx = 0;

    const char *colorname(fpsent *d, const char *name, const char *prefix, const char *suffix, const char *alt)
    {
        if(!name) name = alt && d == player1 ? alt : d->name; 
        bool dup = !name[0] || duplicatename(d, name, alt) || d->aitype != AI_NONE;
        if(dup || prefix[0] || suffix[0])
        {
            cidx = (cidx+1)%3;
            if(dup) formatstring(cname[cidx], d->aitype == AI_NONE ? "%s%s \fs\f5(%d)\fr%s" : "%s%s \fs\f5[%d]\fr%s", prefix, name, d->clientnum, suffix);
            else formatstring(cname[cidx], "%s%s%s", prefix, name, suffix);
            return cname[cidx];
        }
        return name;
    }

    VARP(teamcolortext, 0, 1, 1);

    const char *teamcolorname(fpsent *d, const char *alt)
    {
        if(!teamcolortext || !m_teammode) return colorname(d, NULL, "", "", alt);
        return colorname(d, NULL, isteam(d->team, player1->team) ? "\fs\f1" : "\fs\f3", "\fr", alt); 
    }

    const char *teamcolor(const char *name, bool sameteam, const char *alt)
    {
        if(!teamcolortext || !m_teammode) return sameteam || !alt ? name : alt;
        cidx = (cidx+1)%3;
        formatstring(cname[cidx], sameteam ? "\fs\f1%s\fr" : "\fs\f3%s\fr", sameteam || !alt ? name : alt);
        return cname[cidx];
    }    
    
    const char *teamcolor(const char *name, const char *team, const char *alt)
    {
        return teamcolor(name, team && isteam(team, player1->team), alt);
    }

    void suicide(physent *d)
    {
        if(d==player1 || (d->type==ENT_PLAYER && ((fpsent *)d)->ai))
        {
            if(d->state!=CS_ALIVE) return;
            fpsent *pl = (fpsent *)d;
            if(!m_mp(gamemode)) killed(pl, pl);
            else
            {
                int seq = (pl->lifesequence<<16)|((lastmillis/1000)&0xFFFF);
                if(pl->suicided!=seq) { addmsg(N_SUICIDE, "rc", pl); pl->suicided = seq; }
            }
        }
    }
    ICOMMAND(suicide, "", (), suicide(player1));

    bool needminimap() { return m_ctf || m_protect || m_hold || m_capture || m_collect; }

    void drawicon(int icon, float x, float y, float sz)
    {
        settexture("packages/hud/items.png");
        float tsz = 0.25f, tx = tsz*(icon%4), ty = tsz*(icon/4);
        gle::defvertex(2);
        gle::deftexcoord0();
        gle::begin(GL_TRIANGLE_STRIP);
        gle::attribf(x,    y);    gle::attribf(tx,     ty);
        gle::attribf(x+sz, y);    gle::attribf(tx+tsz, ty);
        gle::attribf(x,    y+sz); gle::attribf(tx,     ty+tsz);
        gle::attribf(x+sz, y+sz); gle::attribf(tx+tsz, ty+tsz);
        gle::end();
    }

    float abovegameplayhud(int w, int h)
    {
        switch(hudplayer()->state)
        {
            case CS_EDITING:
            case CS_SPECTATOR:
                return 1;
            default:
                return 1600.0f/1800.0f;
        }
    }

    int ammohudup[3] = { GUN_CG, GUN_RL, GUN_GL },
        ammohuddown[3] = { GUN_RIFLE, GUN_SG, GUN_PISTOL },
        ammohudcycle[7] = { -1, -1, -1, -1, -1, -1, -1 };

    ICOMMAND(ammohudup, "V", (tagval *args, int numargs),
    {
        loopi(3) ammohudup[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    ICOMMAND(ammohuddown, "V", (tagval *args, int numargs),
    {
        loopi(3) ammohuddown[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    ICOMMAND(ammohudcycle, "V", (tagval *args, int numargs),
    {
        loopi(7) ammohudcycle[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    VARP(ammohud, 0, 1, 1);

    void drawammohud(fpsent *d)
    {
        float x = HICON_X + 2*HICON_STEP, y = HICON_Y, sz = HICON_SIZE;
        pushhudmatrix();
        hudmatrix.scale(1/3.2f, 1/3.2f, 1);
        flushhudmatrix();
        float xup = (x+sz)*3.2f, yup = y*3.2f + 0.1f*sz;
        loopi(3)
        {
            int gun = ammohudup[i];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->hasammo(gun)) continue;
            drawicon(HICON_FIST+gun, xup, yup, sz);
            yup += sz;
        }
        float xdown = x*3.2f - sz, ydown = (y+sz)*3.2f - 0.1f*sz;
        loopi(3)
        {
            int gun = ammohuddown[3-i-1];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->hasammo(gun)) continue;
            ydown -= sz;
            drawicon(HICON_FIST+gun, xdown, ydown, sz);
        }
        int offset = 0, num = 0;
        loopi(7)
        {
            int gun = ammohudcycle[i];
            if(gun < GUN_FIST || gun > GUN_PISTOL) continue;
            if(gun == d->gunselect) offset = i + 1;
            else if(d->ammo[gun]) num++;
        }
        float xcycle = (x+sz/2)*3.2f + 0.5f*num*sz, ycycle = y*3.2f-sz;
        loopi(7)
        {
            int gun = ammohudcycle[(i + offset)%7];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->hasammo(gun)) continue;
            xcycle -= sz;
            drawicon(HICON_FIST+gun, xcycle, ycycle, sz);
        }
        pophudmatrix();
    }

    void drawhudicons(fpsent *d)
    {
        pushhudmatrix();
        hudmatrix.scale(2, 2, 1);
        flushhudmatrix();

        draw_textf("%d", (HICON_X + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, d->state==CS_DEAD ? 0 : d->health);
        if(d->state!=CS_DEAD)
        {
            if(d->armour) draw_textf("%d", (HICON_X + HICON_STEP + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, d->armour);
            const int ammox = (HICON_X + 2*HICON_STEP + HICON_SIZE + HICON_SPACE)/2, ammoy = HICON_TEXTY/2;
            if(guns[d->gunselect].capacity) draw_textf("%d\\%d", ammox, ammoy, d->magazine[d->gunselect], d->ammo[d->gunselect]);
            else draw_textf("%d", ammox, ammoy, d->ammo[d->gunselect]);
        }

        pophudmatrix();

        drawicon(HICON_HEALTH, HICON_X, HICON_Y);
        if(d->state!=CS_DEAD)
        {
            if(d->armour) drawicon(HICON_BLUE_ARMOUR+d->armourtype, HICON_X + HICON_STEP, HICON_Y);
            drawicon(HICON_FIST+d->gunselect, HICON_X + 2*HICON_STEP, HICON_Y);
            if(d->quad.millis) drawicon(HICON_QUAD, HICON_X + 3*HICON_STEP, HICON_Y);
            if(ammohud) drawammohud(d);
        }
    }

    VARP(showphyscompass, 0, 0, 1);

    void drawphyscompassvector(const bvec &color, const float yaw, const float magnitude)
    {
        pushhudmatrix();
        hudmatrix.rotate_around_z(yaw);
        flushhudmatrix();
        gle::defvertex(2);
        gle::color(color, 0xff);
        gle::begin(GL_TRIANGLE_STRIP);
        const float w = 10.0f, h = magnitude * 150.0f;
        const float x = -0.5f * w, y = 0.0f;
        gle::attribf(x    , y);
        gle::attribf(x + w, y);
        gle::attribf(x    , y + h);
        gle::attribf(x + w, y + h);
        gle::end();
        pophudmatrix();
    }

    void drawphyscompass(fpsent *d, int w, int h)
    {
        enabletexture(false);
        pushhudmatrix();
        hudmatrix.translate(vec(900.0f, 1600.0f, 0.0f));
        hudmatrix.rotate_around_z(90.0f * RAD);
        flushhudmatrix();

        const float inputyaw = atan2f(d->fmove, d->fstrafe);
        const float inputmag = min(1.0f, vec2(d->fmove, d->fstrafe).magnitude());
        drawphyscompassvector(bvec(0xff, 0x00, 0x00), inputyaw, inputmag);

        const float velyaw = atan2f(d->vel.y, d->vel.x) - d->yaw*RAD;
        const float velmag = min(3.0f, d->vel.magnitude2() / d->maxspeed);
        drawphyscompassvector(bvec(0xff, 0xff, 0xff), velyaw, velmag);

        pophudmatrix();
        enabletexture(true);
    }

    void gameplayhud(int w, int h)
    {
        if(*customhud)
        {
            hudstate::scrw = w;
            hudstate::scrh = h;
            hudstate::draw();
        }
        pushhudmatrix();
        hudmatrix.scale(h/1800.0f, h/1800.0f, 1);
        flushhudmatrix();

        int pw, ph, tw, th, fw, fh;
        if(spectating(player1))
        {
            text_bounds("  ", pw, ph);
            text_bounds("SPECTATOR", tw, th);
            th = max(th, ph);
            fpsent *f = followingplayer();
            text_bounds(f ? colorname(f) : " ", fw, fh);
            fh = max(fh, ph);
            draw_text("SPECTATOR", w*1800/h - tw - pw, 1650 - th - fh);
            if(f) 
            {
                int color = f->state!=CS_DEAD ? 0xFFFFFF : 0x606060;
                if(f->privilege)
                {
                    color = f->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(f->state==CS_DEAD) color = (color>>1)&0x7F7F7F;
                }
                draw_text(colorname(f), w*1800/h - fw - pw, 1650 - fh, (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF);
            }
        }

        if(!*customhud)
        {
            const float middlex = w*900.0f/h;
            if(m_timed && maplimit >= 0)
            {
                int secs = clockmillis()/1000, mins = secs/60;
                secs %= 60;
                defformatstring(sn, "%d:%02d", mins, secs);
                text_bounds(sn, tw, th);
                pushhudmatrix();
                const float clocksize = 2.0f;
                hudmatrix.scale(clocksize, clocksize, 1.0f);
                flushhudmatrix();
                draw_text(sn, middlex/clocksize - tw/2, 0);
                pophudmatrix();
            }
            const char *message = currentbroadcast();
            if(*message)
            {
                text_bounds(message, tw, th);
                draw_text(message, middlex - tw/2, 200);
            }
        }

        fpsent *d = hudplayer();
        if(d->state!=CS_EDITING)
        {
            if(showphyscompass) drawphyscompass(d, w, h);
            if(!*customhud && d->state!=CS_SPECTATOR) drawhudicons(d);
            if(cmode) cmode->drawhud(d, w, h);
        }

        pophudmatrix();
    }

    int clipconsole(int w, int h)
    {
        if(cmode) return cmode->clipconsole(w, h);
        return 0;
    }

    VARP(teamcrosshair, 0, 1, 1);
    VARP(hitcrosshair, 0, 425, 1000);

    const char *defaultcrosshair(int index)
    {
        switch(index)
        {
            case 2: return "data/hit.png";
            case 1: return "data/teammate.png";
            default: return "data/crosshair.png";
        }
    }

    int selectcrosshair(vec &color)
    {
        fpsent *d = hudplayer();
        if(d->state==CS_SPECTATOR || d->state==CS_DEAD) return -1;

        if(d->state!=CS_ALIVE) return 0;

        int crosshair = 0;
        if(lasthit && lastmillis - lasthit < hitcrosshair) crosshair = 2;
        else if(teamcrosshair)
        {
            dynent *o = intersectclosest(d->o, worldpos, d);
            if(o && o->type==ENT_PLAYER && isteam(((fpsent *)o)->team, d->team))
            {
                crosshair = 1;
                color = vec(0, 0, 1);
            }
        }

        if(crosshair!=1 && !editmode && !m_insta)
        {
            if(d->health<=25) color = vec(1, 0, 0);
            else if(d->health<=50) color = vec(1, 0.5f, 0);
        }
        if(d->gunwait) color.mul(0.5f);
        return crosshair;
    }

    void lighteffects(dynent *e, vec &color, vec &dir)
    {
#if 0
        fpsent *d = (fpsent *)e;
        if(d->state!=CS_DEAD && d->quadmillis)
        {
            float t = 0.5f + 0.5f*sinf(2*M_PI*lastmillis/1000.0f);
            color.y = color.y*(1-t) + t;
        }
#endif
    }

    bool serverinfostartcolumn(g3d_gui *g, int i)
    {
        static const char * const names[] = { "ping ", "players ", "mode ", "map ", "time ", "master ", "host ", "port ", "description " };
        static const float struts[] =       { 7,       7,          12.5f,   14,      7,      8,         14,      7,       24.5f };
        if(size_t(i) >= sizeof(names)/sizeof(names[0])) return false;
        g->pushlist();
        g->text(names[i], 0xFFFF80, !i ? " " : NULL);
        if(struts[i]) g->strut(struts[i]);
        g->mergehits(true);
        return true;
    }

    void serverinfoendcolumn(g3d_gui *g, int i)
    {
        g->mergehits(false);
        g->column(i);
        g->poplist();
    }

    const char *mastermodecolor(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodecolors)/sizeof(mastermodecolors[0])) ? mastermodecolors[n-MM_START] : unknown;
    }

    const char *mastermodeicon(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodeicons)/sizeof(mastermodeicons[0])) ? mastermodeicons[n-MM_START] : unknown;
    }

    bool serverinfoentry(g3d_gui *g, int i, const char *name, int port, const char *sdesc, const char *map, int ping, const vector<int> &attr, int np)
    {
        if(ping < 0 || attr.empty() || attr[0]!=PROTOCOL_VERSION)
        {
            switch(i)
            {
                case 0:
                    if(g->button(" ", 0xFFFFDD, "serverunk")&G3D_UP) return true;
                    break;

                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                    if(g->button(" ", 0xFFFFDD)&G3D_UP) return true;
                    break;

                case 6:
                    if(g->buttonf("%s ", 0xFFFFDD, NULL, name)&G3D_UP) return true;
                    break;

                case 7:
                    if(g->buttonf("%d ", 0xFFFFDD, NULL, port)&G3D_UP) return true;
                    break;

                case 8:
                    if(ping < 0)
                    {
                        if(g->button(sdesc, 0xFFFFDD)&G3D_UP) return true;
                    }
                    else if(g->buttonf("[%s protocol] ", 0xFFFFDD, NULL, attr.empty() ? "unknown" : (attr[0] < PROTOCOL_VERSION ? "older" : "newer"))&G3D_UP) return true;
                    break;
            }
            return false;
        }

        switch(i)
        {
            case 0:
            {
                const char *icon = attr.inrange(3) && np >= attr[3] ? "serverfull" : (attr.inrange(4) ? mastermodeicon(attr[4], "serverunk") : "serverunk");
                if(g->buttonf("%d ", 0xFFFFDD, icon, ping)&G3D_UP) return true;
                break;
            }

            case 1:
                if(attr.length()>=4)
                {
                    if(g->buttonf(np >= attr[3] ? "\f3%d/%d " : "%d/%d ", 0xFFFFDD, NULL, np, attr[3])&G3D_UP) return true;
                }
                else if(g->buttonf("%d ", 0xFFFFDD, NULL, np)&G3D_UP) return true;
                break;

            case 2:
                if(g->buttonf("%s ", 0xFFFFDD, NULL, attr.length()>=2 ? server::modename(attr[1], "") : "")&G3D_UP) return true;
                break;

            case 3:
                if(g->buttonf("%.25s ", 0xFFFFDD, NULL, map)&G3D_UP) return true;
                break;

            case 4:
                if(attr.length()>=3 && attr[2] > 0)
                {
                    int secs = clamp(attr[2], 0, 59*60+59),
                        mins = secs/60;
                    secs %= 60;
                    if(g->buttonf("%d:%02d ", 0xFFFFDD, NULL, mins, secs)&G3D_UP) return true;
                }
                else if(g->buttonf(" ", 0xFFFFDD)&G3D_UP) return true;
                break;
            case 5:
                if(g->buttonf("%s%s ", 0xFFFFDD, NULL, attr.length()>=5 ? mastermodecolor(attr[4], "") : "", attr.length()>=5 ? server::mastermodename(attr[4], "") : "")&G3D_UP) return true;
                break;

            case 6:
                if(g->buttonf("%s ", 0xFFFFDD, NULL, name)&G3D_UP) return true;
                break;

            case 7:
                if(g->buttonf("%d ", 0xFFFFDD, NULL, port)&G3D_UP) return true;
                break;

            case 8:
                if(g->buttonf("%.25s", 0xFFFFDD, NULL, sdesc)&G3D_UP) return true;
                break;
        }
        return false;
    }

    // any data written into this vector will get saved with the map data. Must take care to do own versioning, and endianess if applicable. Will not get called when loading maps from other games, so provide defaults.
    void writegamedata(vector<char> &extras) {}
    void readgamedata(vector<char> &extras) {}

    const char *savedconfig() { return "config.cfg"; }
    const char *restoreconfig() { return "restore.cfg"; }
    const char *defaultconfig() { return "data/defaults.cfg"; }
    const char *autoexec() { return "autoexec.cfg"; }
    const char *savedservers() { return "servers.cfg"; }

    void loadconfigs()
    {
        if(identexists("playsong")) execute("playsong");

        execfile("auth.cfg", false);
    }
}

