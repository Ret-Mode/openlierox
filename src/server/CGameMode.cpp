/*
 *  CGameMode.cpp
 *  OpenLieroX
 *
 *  Created by Albert Zeyer on 02.03.09.
 *  code under LGPL
 *
 */

#include "CGameMode.h"
#include "CServer.h"
#include "Options.h"
#include "CWorm.h"
#include "Protocol.h"

int CGameMode::GeneralGameType() {
	if(GameTeams() == 1)
		return GMT_NORMAL;
	else
		return GMT_TEAMS;
}


int CGameMode::GameTeams() {
	return 1;
}

float CGameMode::TimeLimit() {
	return tLXOptions->tGameInfo.fTimeLimit * 60.0f;
}

std::string CGameMode::TeamName(int t) {
	static const std::string teamnames[4] = { "blue", "red", "green", "yellow" };
	if(t >= 0 && t < 4) return teamnames[t];
	return itoa(t);
}

void CGameMode::PrepareGame()
{
	bFirstBlood = true;
	for(int i = 0; i < MAX_WORMS; i++) {
		iKillsInRow[i] = 0;
		iDeathsInRow[i] = 0;
	}
}

bool CGameMode::Spawn(CWorm* worm, CVec pos) {
	worm->Spawn(pos);
	return true;
}

void CGameMode::Kill(CWorm* victim, CWorm* killer)
{
	// Kill or suicide message
	std::string buf;
	if( killer )
	{
		if( killer == victim )
		{
			// Suicide
			// TODO: Restore the suicide count message
			if( networkTexts->sCommitedSuicide != "<none>" )
				replacemax(networkTexts->sCommitedSuicide, "<player>", victim->getName(), buf, 1);
			if( tLXOptions->tGameInfo.features[FT_SuicideDecreasesScore] && killer->getKills() > 0 )
				killer->setKills(killer->getKills()-1);
		}
		else if( GameTeams() > 1 && killer->getTeam() == victim->getTeam() )
		{
			if( networkTexts->sTeamkill != "<none>" )
				replacemax(networkTexts->sTeamkill, "<player>", killer->getName(), buf, 1);
			if( tLXOptions->tGameInfo.features[FT_TeamkillDecreasesScore] && killer->getKills() > 0 )
				killer->setKills(killer->getKills()-1);
			if( tLXOptions->tGameInfo.features[FT_CountTeamkills] )
				killer->AddKill();
		}
		else
		{
			if(networkTexts->sKilled != "<none>")
			{
				replacemax(networkTexts->sKilled, "<killer>", killer->getName(), buf, 1);
				replacemax(buf, "<victim>", victim->getName(), buf, 1);
			}
			killer->AddKill();
		}
	}
	else
	{
		// TODO: message if no killer
	}
	if( buf != "" )
		cServer->SendGlobalText(buf, TXT_NORMAL);
	
	// First blood
	if(bFirstBlood && killer && killer != victim && networkTexts->sFirstBlood != "<none>")  {
		bFirstBlood = false;
		cServer->SendGlobalText(replacemax(networkTexts->sFirstBlood, "<player>",
								killer->getName(), 1), TXT_NORMAL);
	}

	// Kills & deaths in row
	if(killer && killer != victim) {
		iKillsInRow[killer->getID()]++;
		iDeathsInRow[killer->getID()] = 0;
	}
	iKillsInRow[victim->getID()] = 0;
	iDeathsInRow[victim->getID()]++;

	// Killing spree message
	if(killer && killer != victim) {
		switch(iKillsInRow[killer->getID()]) {
			case 3:
				if(networkTexts->sSpree1 != "<none>")
					cServer->SendGlobalText(replacemax(networkTexts->sSpree1, "<player>",
											killer->getName(), 1), TXT_NORMAL);
				break;
			case 5:
				if(networkTexts->sSpree2 != "<none>")
					cServer->SendGlobalText(replacemax(networkTexts->sSpree2, "<player>",
											killer->getName(), 1), TXT_NORMAL);
				break;
			case 7:
				if(networkTexts->sSpree3 != "<none>")
					cServer->SendGlobalText(replacemax(networkTexts->sSpree3, "<player>",
											killer->getName(), 1), TXT_NORMAL);
				break;
			case 9:
				if(networkTexts->sSpree4 != "<none>")
					cServer->SendGlobalText(replacemax(networkTexts->sSpree4, "<player>",
											killer->getName(), 1), TXT_NORMAL);
				break;
			case 10:
				if(networkTexts->sSpree5 != "<none>")
					cServer->SendGlobalText(replacemax(networkTexts->sSpree5, "<player>",
											killer->getName(), 1), TXT_NORMAL);
				break;
		}
	}

	// Dying spree message
	switch(iDeathsInRow[victim->getID()]) {
		case 3:
			if(networkTexts->sDSpree1 != "<none>")
				cServer->SendGlobalText(replacemax(networkTexts->sDSpree1, "<player>",
										victim->getName(), 1), TXT_NORMAL);
			break;
		case 5:
			if(networkTexts->sDSpree2 != "<none>")
				cServer->SendGlobalText(replacemax(networkTexts->sDSpree2, "<player>",
										victim->getName(), 1), TXT_NORMAL);
			break;
		case 7:
			if(networkTexts->sDSpree3 != "<none>")
				cServer->SendGlobalText(replacemax(networkTexts->sDSpree3, "<player>",
										victim->getName(), 1), TXT_NORMAL);
			break;
		case 9:
			if(networkTexts->sDSpree4 != "<none>")
				cServer->SendGlobalText(replacemax(networkTexts->sDSpree4, "<player>",
										victim->getName(), 1), TXT_NORMAL);
			break;
		case 10:
			if(networkTexts->sDSpree5 != "<none>")
				cServer->SendGlobalText(replacemax(networkTexts->sDSpree5, "<player>",
										victim->getName(), 1), TXT_NORMAL);
			break;
	}

	// Victim is out of the game
	if(victim->Kill() && networkTexts->sPlayerOut != "<none>")
		cServer->SendGlobalText(replacemax(networkTexts->sPlayerOut, "<player>",
								victim->getName(), 1), TXT_NORMAL);

	if( GameTeams() > 1 )
	{
		int worms[4] = { 0, 0, 0, 0 };
		for(int i = 0; i < MAX_WORMS; i++)
			if(cServer->getWorms()[i].isUsed() && cServer->getWorms()[i].getLives() != WRM_OUT)
				if(cServer->getWorms()[i].getTeam() >= 0 && cServer->getWorms()[i].getTeam() < 4)
					worms[cServer->getWorms()[i].getTeam()]++;
	
		// Victim's team is out of the game
		if(worms[victim->getTeam()] == 0 && networkTexts->sTeamOut != "<none>")
			cServer->SendGlobalText(replacemax(networkTexts->sTeamOut, "<team>",
				TeamName(victim->getTeam()), 1), TXT_NORMAL);
	}
}

void CGameMode::Drop(CWorm* worm)
{
	if (!worm || worm->getID() < 0 || worm->getID() >= MAX_WORMS) {
		errors << "Dropped an invalid worm" << endl;
		return;
	}
	
	iKillsInRow[worm->getID()] = 0;
	iDeathsInRow[worm->getID()] = 0;
}

static int getWormHitKillLimit() {
	if(tLXOptions->tGameInfo.iKillLimit <= 0) return -1;
	
	for(int i = 0; i < MAX_WORMS; ++i) {
		CWorm* w = &cServer->getWorms()[i];
		if(!w->isUsed()) continue;
		if(w->getKills() >= tLXOptions->tGameInfo.iKillLimit)
			return i;
	}
	
	return -1;
}

bool CGameMode::CheckGameOver() {
	// In game?
	if (!cServer || cServer->getState() == SVS_LOBBY || cServer->getGameOver())
		return true;

	// check for teamscorelimit
	if(GameTeams() > 1 && (int)tLXOptions->tGameInfo.features[FT_TeamScoreLimit] > 0) {
		for(int i = 0; i < GameTeams(); ++i) {
			if(TeamScores(i) >= (int)tLXOptions->tGameInfo.features[FT_TeamScoreLimit]) {
				// TODO: make configureable
				cServer->SendGlobalText("Team score limit " + itoa((int)tLXOptions->tGameInfo.features[FT_TeamScoreLimit]) + " hit by " + TeamName(i), TXT_NORMAL);
				notes << "team score limit hit by team " << i << endl;
				return true;
			}
		}
	}

	// check for maxkills
	if(tLXOptions->tGameInfo.iKillLimit > 0) {
		int w = getWormHitKillLimit();
		if(w >= 0) {
			// TODO: make configureable
			cServer->SendGlobalText("Kill limit " + itoa(tLXOptions->tGameInfo.iKillLimit) + " hit by worm " + cServer->getWorms()[w].getName(), TXT_NORMAL);
			notes << "worm " << w << " hit the kill limit" << endl;
			return true;
		}
	}
	
	// Check if the timelimit has been reached
	if(TimeLimit() > 0) {
		if (cServer->getServerTime() > TimeLimit()) {
			if(networkTexts->sTimeLimit != "<none>")
				cServer->SendGlobalText(networkTexts->sTimeLimit, TXT_NORMAL);
			notes << "time limit (" << TimeLimit() << ") reached with current time " << cServer->getServerTime().seconds();
			notes << " -> game over" << endl;
			return true;
		}
	}
	
	bool allowEmptyGames =
		tLXOptions->tGameInfo.features[FT_AllowEmptyGames] &&
		tLXOptions->tGameInfo.bAllowConnectDuringGame &&
		tLX->iGameType != GME_LOCAL &&
		tLXOptions->tGameInfo.iLives < 0;
	
	if(!allowEmptyGames) {
		// TODO: move that worms-num calculation to GameServer
		int worms = 0;
		for(int i = 0; i < MAX_WORMS; i++)
			if(cServer->getWorms()[i].isUsed()) {
				if (cServer->getWorms()[i].getLives() != WRM_OUT)
					worms++;
			}

		int minWormsNeeded = 2;
		if(tLX->iGameType == GME_LOCAL && cServer->getNumPlayers() == 1) minWormsNeeded = 1;
		if(worms < minWormsNeeded) {
			// TODO: make configureable
			cServer->SendGlobalText("Too less players in game", TXT_NORMAL);
			notes << "only " << worms << " players left in game -> game over" << endl;
			return true;
		}
	}

	if(!allowEmptyGames && GameTeams() > 1) {
		// Only one team left?
		int teams = 0;
		for(int i = 0; i < GameTeams(); i++)  {
			if(WormsAliveInTeam(i)) {
				teams++;
			}
		}
		
		// Only one team left
		if(teams <= 1) {
			// TODO: make configureable
			cServer->SendGlobalText("Too less teams in game", TXT_NORMAL);
			return true;
		}
	}
	
	return false;
}

int CGameMode::Winner() {
	return HighestScoredWorm();
}

int CGameMode::HighestScoredWorm() {
	int wormid = -1;
	for(int i = 0; i < MAX_WORMS; i++)
		if(cServer->getWorms()[i].isUsed()) {
			if(wormid < 0) wormid = i;
			else
				wormid = CompareWormsScore(&cServer->getWorms()[i], &cServer->getWorms()[wormid]) > 0 ? i : wormid; // Take the worm with the best score
		}
	
	return wormid;
}

int CGameMode::CompareWormsScore(CWorm* w1, CWorm* w2) {
	// Kills very first (in case there was a kill limit)
	if(cClient->getGameLobby()->iKillLimit > 0) {
		if (w1->getKills() > w2->getKills()) return 1;
		if (w1->getKills() < w2->getKills()) return -1;		
	}
	
	// Lives first
	if(cClient->getGameLobby()->iLives >= 0) {
		if (w1->getLives() > w2->getLives()) return 1;
		if (w1->getLives() < w2->getLives()) return -1;		
	}
	
	// Kills
	if (w1->getKills() > w2->getKills()) return 1;
	if (w1->getKills() < w2->getKills()) return -1;
	
	// Damage
	return Round(w1->getDamage() - w2->getDamage());
}

int CGameMode::WinnerTeam() {
	if(GameTeams() > 1)
		return HighestScoredTeam();
	else
		return -1;
}

int CGameMode::HighestScoredTeam() {
	int team = -1;
	for(int i = 0; i < GameTeams(); i++)
		if(team < 0) team = i;
		else
			team = CompareTeamsScore(i, team) > 0 ? i : team; // Take the team with the best score
	
	return team;
}

int CGameMode::WormsAliveInTeam(int t) {
	int c = 0;
	for(int i = 0; i < MAX_WORMS; i++)
		if(cServer->getWorms()[i].isUsed() && cServer->getWorms()[i].getLives() != WRM_OUT && cServer->getWorms()[i].getTeam() == t)
			c++;
	return c;
}

int CGameMode::TeamScores(int t) {
	return TeamKills(t);
}

int CGameMode::TeamKills(int t) {
	int c = 0;
	for(int i = 0; i < MAX_WORMS; i++)
		if(cServer->getWorms()[i].isUsed() && cServer->getWorms()[i].getTeam() == t)
			c += cServer->getWorms()[i].getKills();
	return c;
}

float CGameMode::TeamDamage(int t) {
	float c = 0;
	for(int i = 0; i < MAX_WORMS; i++)
		if(cServer->getWorms()[i].isUsed() && cServer->getWorms()[i].getTeam() == t)
			c += cServer->getWorms()[i].getDamage();
	return c;
}


int CGameMode::CompareTeamsScore(int t1, int t2) {
	// Lives first
	if(tLXOptions->tGameInfo.iLives >= 0) {
		int d = WormsAliveInTeam(t1) - WormsAliveInTeam(t2);
		if(d != 0) return d;
	}
	
	// if there is a custom teamscore function
	{
		int d = TeamScores(t1) - TeamScores(t2);
		if(d != 0) return d;
	}
	
	// Kills
	{
		int d = TeamKills(t1) - TeamKills(t2);
		if(d != 0) return d;
	}
	
	// Damage
	return Round(TeamDamage(t1) - TeamDamage(t2));
}


extern CGameMode* gameMode_DeathMatch;
extern CGameMode* gameMode_TeamDeathMatch;
extern CGameMode* gameMode_Tag;
extern CGameMode* gameMode_Demolitions;
extern CGameMode* gameMode_HideAndSeek;
extern CGameMode* gameMode_CaptureTheFlag;
extern CGameMode* gameMode_Race;
extern CGameMode* gameMode_TeamRace;


static std::vector<CGameMode*> gameModes;

void InitGameModes() {
	gameModes.resize(8);
	gameModes[0] = gameMode_DeathMatch;
	gameModes[1] = gameMode_TeamDeathMatch;
	gameModes[2] = gameMode_Tag;
	gameModes[3] = gameMode_Demolitions;
	gameModes[4] = gameMode_HideAndSeek;
	gameModes[5] = gameMode_CaptureTheFlag;
	gameModes[6] = gameMode_Race;
	gameModes[7] = gameMode_TeamRace;
}

CGameMode* GameMode(GameModeIndex i) {
	if(i < 0 || (uint)i >= gameModes.size()) {
		errors << "gamemode " << i << " requested, we don't have such one" << endl;
		return NULL;
	}
	
	return gameModes[i];
}

CGameMode* GameMode(const std::string& name) {
	for(std::vector<CGameMode*>::iterator i = gameModes.begin(); i != gameModes.end(); ++i) {
		if(name == (*i)->Name())
			return *i;
	}
	warnings << "gamemode " << name << " requested, we don't have such one" << endl;
	return NULL;
}

GameModeIndex GetGameModeIndex(CGameMode* gameMode) {
	if(gameMode == NULL) return GM_DEATHMATCH;
	int index = 0;
	for(std::vector<CGameMode*>::iterator i = gameModes.begin(); i != gameModes.end(); ++i, ++index) {
		if(gameMode == *i)
			return (GameModeIndex)index;
	}
	return GM_DEATHMATCH;
}



Iterator<CGameMode* const&>::Ref GameModeIterator() {
	return GetConstIterator(gameModes);
}

std::string guessGeneralGameTypeName(int iGeneralGameType)
{
	if( iGeneralGameType == GMT_NORMAL )
		return "Death Match";
	if( iGeneralGameType == GMT_TEAMS )
		return "Team Death Match";
	if( iGeneralGameType == GMT_TIME )
		return "Tag";
	if( iGeneralGameType == GMT_DIRT )
		return "Demolitions";
	return "";
};
