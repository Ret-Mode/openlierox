/*
 *  FlagInfo.cpp
 *  OpenLieroX
 *
 *  Created by Albert Zeyer on 20.03.09.
 *  code under LGPL
 *
 */

// TODO: remove unnecessary includes
#include <SDL.h>
#include <map>
#include "Debug.h"
#include "FlagInfo.h"
#include "CWorm.h"
#include "CServer.h"
#include "CClient.h"
#include "LieroX.h"
#include "CGameMode.h"
#include "CBytestream.h"
#include "CServerConnection.h"
#include "CServerNetEngine.h"
#include "CViewport.h"
#include "CGameSkin.h"
#include "DeprecatedGUI/Graphics.h"
#include "GfxPrimitives.h"
#include "CMap.h"

#define FLAG_FRAME_WIDTH 32
#define FLAG_FRAME_HEIGHT 18
#define FLAG_SPACING 4
#define FLAG_WIDTH 18
#define FLAG_HEIGHT 18

Flag::Flag(int i) : id(i), holderWorm(-1), atSpawnPoint(true), skin(NULL) {
	skin = new CGameSkin("../data/gfx/flags.png", FLAG_FRAME_WIDTH, FLAG_FRAME_HEIGHT, FLAG_SPACING, FLAG_WIDTH, FLAG_HEIGHT);
	
	if(i >= 0 && i < 4) {
		skin->Colorize(tLX->clTeamColors[i]);
	}
}

Flag& Flag::operator=(const Flag& f) {
	id = f.id;
	pos = f.pos;
	holderWorm = f.holderWorm;
	spawnPoint = f.spawnPoint;
	atSpawnPoint = f.atSpawnPoint;
	if(f.skin)
		skin = new CGameSkin(*f.skin);
	else
		skin = NULL;
	return *this;
}

Flag::~Flag() {
	if(skin) {
		delete skin;
		skin = NULL;
	}
}

CVec Flag::getPos() {
	if(atSpawnPoint)
		return spawnPoint.pos;
	if(holderWorm >= 0) {
		if(tLX->iGameType == GME_JOIN)
			return cClient->getRemoteWorms()[holderWorm].getPos();
		else
			return cServer->getWorms()[holderWorm].getPos();
	}
	return pos;
}

typedef std::map<int,Flag> Flags;

struct FlagInfoData {
	Flags flags;
};

FlagInfo::FlagInfo() {
	data = new FlagInfoData();
	reset();
}

FlagInfo::~FlagInfo() {
	delete data; data = NULL;
}

void FlagInfo::reset() {
	data->flags.clear();
}

Flag* FlagInfo::initFlag(int id) {
	return &( data->flags[id] = Flag(id) );
}

Flag* FlagInfo::getFlag(int id) {
	Flags::iterator i = data->flags.find(id);
	if(i != data->flags.end())
		return &i->second;
	else
		return NULL;
}

Flag* FlagInfo::getFlagOfWorm(int worm) {
	for(Flags::iterator i = data->flags.begin(); i != data->flags.end(); ++i) {
		if(!i->second.atSpawnPoint && i->second.holderWorm == worm)
			return &i->second;
	}
	return NULL;
}

bool FlagInfo::removeFlag(int id) {
	Flags::iterator i = data->flags.find(id);
	if(i != data->flags.end()) {
		data->flags.erase(i);
		return true;
	}
	return false;
}


static void drawFlagSpawnPoint(Flag* flag, SDL_Surface* bmpDest, CViewport* v) {
	SDL_Surface* bmp = NULL;
	if(flag->id >= 0 && flag->id < 4)
		bmp = DeprecatedGUI::gfxGame.bmpFlagSpawnpoint[flag->id].get();
	else
		bmp = DeprecatedGUI::gfxGame.bmpFlagSpawnpointDefault.get();
	
	int l = v->GetLeft();
	int t = v->GetTop();
	int x = (int) ( (flag->spawnPoint.pos.x - v->GetWorldX()) * 2 + l );
	int y = (int) ( (flag->spawnPoint.pos.y - v->GetWorldY()) * 2 + t );
	x -= x % 2;
	y -= y % 2;
	
	DrawImage(bmpDest, bmp, x - bmp->w/2, y - bmp->h/2);
}

static void drawUnattachedFlag(Flag* flag, SDL_Surface* bmpDest, CViewport* v) {
	if(flag->skin == NULL) return;
	
	int l = v->GetLeft();
	int t = v->GetTop();
	int x = (int) ( (flag->getPos().x - v->GetWorldX()) * 2 + l );
	int y = (int) ( (flag->getPos().y - v->GetWorldY()) * 2 + t );
	x -= x % 2;
	y -= y % 2;
	
	int f = ((int) cClient->serverTime().seconds() *7);
	f %= flag->skin->getFrameCount(); // every skin has exactly 21 frames
	
	flag->skin->Draw(bmpDest, x - FLAG_WIDTH/2, y - FLAG_HEIGHT/2, f, false, true);
}

void FlagInfo::draw(SDL_Surface* bmpDest, CViewport* v) {
	for(Flags::iterator i = data->flags.begin(); i != data->flags.end(); ++i) {
		Flag& flag = i->second;

		drawFlagSpawnPoint(&flag, bmpDest, v);
		
		if(flag.holderWorm >= 0) continue;
		
		// drawing unattached or flags at spawnpoint
		drawUnattachedFlag(&flag, bmpDest, v);
	}
}

void FlagInfo::drawWormAttachedFlag(CWorm* worm, SDL_Surface* bmpDest, CViewport* v) {
	Flag* flag = getFlagOfWorm(worm->getID());
	if(flag == NULL) return;
	if(flag->skin == NULL) return;
	
	// see CWorm::Draw() for all these calculations
	
	int l = v->GetLeft();
	int t = v->GetTop();
	int x = (int) ( (worm->getPos().x - v->GetWorldX()) * 2 + l );
	int y = (int) ( (worm->getPos().y - v->GetWorldY()) * 2 + t );
	x -= x % 2;
	y -= y % 2;

	int f = ((int) worm->frame()*7);
	int ang = (int)( (worm->getAngle()+90)/151 * 7 );
	f += ang;
	
	if(worm->getDirection() == DIR_LEFT) {
		flag->skin->Draw(bmpDest, x - flag->skin->getSkinWidth(), y - flag->skin->getSkinHeight(), f, false, true);
	}
	else {
		flag->skin->Draw(bmpDest, x, y - flag->skin->getSkinHeight(), f, false, false);
	}
}

void FlagInfo::drawOnMiniMap(CMap* cMap, SDL_Surface* bmpDest, uint miniX, uint miniY) {
	for(Flags::iterator i = data->flags.begin(); i != data->flags.end(); ++i) {
		Flag& flag = i->second;

		Uint8 r = 255,g=255,b=255,a=255;
		if(flag.id >= 0 && flag.id < 4)
			GetColour4(tLX->clTeamColors[flag.id], getMainPixelFormat(), &r,&g,&b,&a);
		
		cMap->drawOnMiniMap(bmpDest, miniX, miniY, flag.spawnPoint.pos, r, g, b, true, true);
		
		if(flag.holderWorm >= 0) continue;
		
		// drawing unattached or flags at spawnpoint
		cMap->drawOnMiniMap(bmpDest, miniX, miniY, flag.getPos(), r, g, b, false, true);
	}
}





void FlagInfo::checkWorm(CWorm* worm) {
	if(tLX->iGameType == GME_JOIN) {
		errors << "FlagInfo::checkWorm: this function is only supposed to be called by the server" << endl;
		return;
	}
	
	if(!worm->getAlive()) return;
	
	for(Flags::iterator i = data->flags.begin(); i != data->flags.end(); ++i) {
		if( (worm->getPos() - i->second.spawnPoint.pos).GetLength2() < 20.0f ) {
			cServer->getGameMode()->hitFlagSpawnPoint(worm, &i->second);
			
			if(i->second.atSpawnPoint)
				cServer->getGameMode()->hitFlag(worm, &i->second);
		}
		
		if(!i->second.atSpawnPoint && i->second.holderWorm < 0) {
			if( (worm->getPos() - i->second.pos).GetLength2() < 20.0f ) {
				cServer->getGameMode()->hitFlag(worm, &i->second);			
			}
		}
	}
}

int FlagInfo::getWidth() const
{
	return FLAG_WIDTH;
}

int FlagInfo::getHeight() const
{
	return FLAG_HEIGHT;
}


enum FlagUpdateType {
	FUT_NEWFLAG = 0,
	FUT_REMOVEFLAG = 1,
	FUT_POS = 2,
	FUT_SPAWNPOS = 3,
	FUT_HOLDERWORM = 4,
	FUT_RESET = 5
};

void FlagInfo::readUpdate(CBytestream* bs) {
	int id = bs->readByte();
	int type = bs->readByte();
	if(type == FUT_NEWFLAG) {
		// spawnpos
		short x, y;
		bs->read2Int12( x, y );
		Flag* flag = initFlag(id);
		flag->spawnPoint.pos = CVec(x,y);
		return;
	}
	if(type == FUT_REMOVEFLAG) {
		if(!removeFlag(id))
			warnings << "FlagInfo::readUpdate: removing flag " << id << " not found" << endl;
		return;
	}

	Flag* flag = getFlag(id);
	if(flag == NULL) {
		warnings << "FlagInfo::readUpdate: flag " << id << " not found" << endl;
		bs->revertByte(); bs->revertByte();
		FlagInfo::skipUpdate(bs);
		return;
	}
	
	switch(type) {
		case FUT_POS: {
			short x, y;
			bs->read2Int12( x, y );
			flag->setCustomPos( CVec(x, y) );
			break;
		}
			
		case FUT_SPAWNPOS: {
			short x, y;
			bs->read2Int12( x, y );
			flag->spawnPoint.pos = CVec(x, y);
			break;
		}
			
		case FUT_HOLDERWORM: {
			int worm = bs->readByte();
			if(worm < 0 || worm >= MAX_WORMS) {
				warnings << "FlagInfo::readUpdate: holder worm id " << worm << " is invalid" << endl;
				return;
			}
			flag->setHolderWorm(worm);
			break;
		}
			
		case FUT_RESET: {
			flag->setBack();
			break;
		}
			
		default: {
			warnings << "FlagInfo::readUpdate: update type " << type << " is invalid" << endl;
			bs->SkipAll(); // probably screwed up anyway
		}
	}
}

void FlagInfo::skipUpdate(CBytestream* bs) {
	/* int id = */ bs->readByte();
	int type = bs->readByte();
	switch(type) {
		case FUT_NEWFLAG: bs->Skip(3); break;
		case FUT_REMOVEFLAG: break;
		case FUT_POS: bs->Skip(3); break;
		case FUT_SPAWNPOS: bs->Skip(3); break;
		case FUT_HOLDERWORM: bs->Skip(1); break;
		case FUT_RESET: break;
		default: {
			warnings << "FlagInfo::skipUpdate: update type " << type << " is invalid" << endl;
			bs->SkipAll(); // probably screwed up anyway
		}
	}
}



Flag* FlagInfo::applyInitFlag(int id, const CVec& spawnPos) {
	if(tLX->iGameType == GME_JOIN) {
		errors << "FlagInfo::applyInitFlag: this function is only supposed to be called by the server" << endl;
		return NULL;
	}
	
	Flag* flag = initFlag(id);
	flag->spawnPoint.pos = spawnPos;
	
	CBytestream bs;
	bs.writeByte(S2C_FLAGINFO);
	bs.writeByte(id);
	bs.writeByte(FUT_NEWFLAG);
	bs.write2Int12((int)spawnPos.x, (int)spawnPos.y);
	cServer->SendGlobalPacket(&bs, OLXBetaVersion(9));
	return flag;
}

void FlagInfo::applyRemoveFlag(int id) {
	if(tLX->iGameType == GME_JOIN) {
		errors << "FlagInfo::applyRemoveFlag: this function is only supposed to be called by the server" << endl;
		return;
	}
	
	removeFlag(id);
	
	CBytestream bs;
	bs.writeByte(S2C_FLAGINFO);
	bs.writeByte(id);
	bs.writeByte(FUT_REMOVEFLAG);
	cServer->SendGlobalPacket(&bs, OLXBetaVersion(9));	
}

void FlagInfo::applyCustomPos(Flag* flag, const CVec& pos) {
	if(tLX->iGameType == GME_JOIN) {
		errors << "FlagInfo::applyCustomPos: this function is only supposed to be called by the server" << endl;
		return;
	}
	
	flag->setCustomPos(pos);

	CBytestream bs;
	bs.writeByte(S2C_FLAGINFO);
	bs.writeByte(flag->id);
	bs.writeByte(FUT_POS);
	bs.write2Int12((int)pos.x, (int)pos.y);
	cServer->SendGlobalPacket(&bs, OLXBetaVersion(9));		
}

void FlagInfo::applySpawnPos(Flag* flag, const CVec& spawnPos) {
	if(tLX->iGameType == GME_JOIN) {
		errors << "FlagInfo::applySpawnPos: this function is only supposed to be called by the server" << endl;
		return;
	}
	
	flag->spawnPoint.pos = spawnPos;
	
	CBytestream bs;
	bs.writeByte(S2C_FLAGINFO);
	bs.writeByte(flag->id);
	bs.writeByte(FUT_SPAWNPOS);
	bs.write2Int12((int)spawnPos.x, (int)spawnPos.y);
	cServer->SendGlobalPacket(&bs, OLXBetaVersion(9));		
}

void FlagInfo::applyHolderWorm(Flag* flag, int w) {
	if(tLX->iGameType == GME_JOIN) {
		errors << "FlagInfo::applyHolderWorm: this function is only supposed to be called by the server" << endl;
		return;
	}
	
	flag->setHolderWorm(w);
	
	CBytestream bs;
	bs.writeByte(S2C_FLAGINFO);
	bs.writeByte(flag->id);
	bs.writeByte(FUT_HOLDERWORM);
	bs.writeByte(w);
	cServer->SendGlobalPacket(&bs, OLXBetaVersion(9));			
}

void FlagInfo::applySetBack(Flag* flag) {
	if(tLX->iGameType == GME_JOIN) {
		errors << "FlagInfo::applySetBack: this function is only supposed to be called by the server" << endl;
		return;
	}
	
	flag->setBack();
	
	CBytestream bs;
	bs.writeByte(S2C_FLAGINFO);
	bs.writeByte(flag->id);
	bs.writeByte(FUT_RESET);
	cServer->SendGlobalPacket(&bs, OLXBetaVersion(9));	
}

void FlagInfo::sendCurrentState(CServerConnection* cl) {
	if(cl->getClientVersion() < OLXBetaVersion(9)) return;
	
	for(Flags::iterator i = data->flags.begin(); i != data->flags.end(); ++i) {
		Flag& flag = i->second;
		
		CBytestream bs;
		bs.writeByte(S2C_FLAGINFO);
		bs.writeByte(flag.id);
		bs.writeByte(FUT_NEWFLAG);
		bs.write2Int12((int)flag.spawnPoint.pos.x, (int)flag.spawnPoint.pos.y);
		
		if(!flag.atSpawnPoint && flag.holderWorm < 0) {
			bs.writeByte(S2C_FLAGINFO);
			bs.writeByte(flag.id);
			bs.writeByte(FUT_POS);
			bs.write2Int12((int)flag.pos.x, (int)flag.pos.y);
		}
		else if(!flag.atSpawnPoint && flag.holderWorm >= 0) {
			bs.writeByte(S2C_FLAGINFO);
			bs.writeByte(flag.id);
			bs.writeByte(FUT_HOLDERWORM);
			bs.writeByte(flag.holderWorm);
		}

		cl->getNetEngine()->SendPacket(&bs);
	}
}
