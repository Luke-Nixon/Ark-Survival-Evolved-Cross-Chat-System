#include <API/ARK/Ark.h>
#include <API/UE/Math/ColorList.h>
#include <Timer.h>
#include <stdlib.h>
#include <iostream>
#include <stdlib.h>
#include <iostream>

#include <mysql+++.h>
#pragma comment(lib, "ArkApi.lib")
#pragma comment(lib, "mysqlclient.lib")

daotk::mysql::connection my;
FString mapname("");

DECLARE_HOOK(AShooterGameMode_InitGame, void, AShooterGameMode*, FString*, FString*, FString*);
DECLARE_HOOK(AShooterPlayerController_ServerSendChatMessage_Impl, void, AShooterPlayerController*, FString*, EChatSendMode::Type);

FString GetTribeName(AShooterPlayerController* playerController)
{
	std::string tribeName;

	auto playerState = reinterpret_cast<AShooterPlayerState*>(playerController->PlayerStateField());
	if (playerState)
	{
		auto tribeData = playerState->MyTribeDataField();
		tribeName = tribeData->TribeNameField().ToString();
	}

	return tribeName.c_str();
}

void ConnectDatabase()
{
	try
	{
		my.open("localhost", "username", "password", "dbname"); // change these for your setup

		if (!my)
		{
			Log::GetLog()->error("MYSQL connection could not be established!?");
		}
		else
		{
			Log::GetLog()->info("MYSQL connection was established sucsessfully. :)");
		}
	}
	catch (const std::exception& ex)
	{
		Log::GetLog()->warn("problem in ConnectDatabase:");
		Log::GetLog()->warn(ex.what());
	}

}

void AShooterPlayerController_ServerSendChatMessage_Impl(AShooterPlayerController* player_controller, FString* message, EChatSendMode::Type mode)
{
	try 
	{
		// only global chat gets sent to the other servers
		if (mode == EChatSendMode::GlobalChat)
		{
			std::string contents = message->ToString();

			FString FPlayername;
			player_controller->GetPlayerCharacterName(&FPlayername);

			std::string player_name = FPlayername.ToString();

			std::string tribename = GetTribeName(player_controller).ToString();

			std::string smapname = mapname.ToString();

			if (!my)
			{
				Log::GetLog()->warn("problem in PostLatestChat with the database");
				ConnectDatabase();
			}
			else
			{
				Log::GetLog()->info("is DB open " + std::to_string(my.is_open() ));

				daotk::mysql::prepared_stmt stmt(my, "INSERT into messages (`name`,`tribe`,`contents`,`originating_map`) VALUES (?,?,?,?);");
				stmt.bind_param(player_name, tribename, contents, smapname);

				stmt.execute();
			}
		}
		AShooterPlayerController_ServerSendChatMessage_Impl_original(player_controller, message, mode);
	}
	catch(const std::exception& ex)
	{
		Log::GetLog()->error(ex.what());
		AShooterPlayerController_ServerSendChatMessage_Impl_original(player_controller, message, mode);
	}
}

int last_read_message_id = 0;
void PostLatestChat()
{
	try {
		
		if (last_read_message_id == 0) // if this is the first attempt to read from the db
		{
			Log::GetLog()->info("Finding last chat message ID in database...");
			// get the last entry ID. and update the ID.
			last_read_message_id = my.query("SELECT `id` FROM `messages` ORDER BY id DESC LIMIT 1;").get_value<int>();
			Log::GetLog()->info("Last message ID in database was: " + std::to_string(last_read_message_id));
		}
		else // if the last read message ID is known, then process all messages in the DB from that ID to the last ID.
		{
			// changed as of 22/05/2023 but not yet deployed. this i think fixes the double post issue. was previously set to DESC
			my.query("SELECT * FROM `messages` WHERE `id` > " + std::to_string(last_read_message_id) + " ORDER BY `id` ASC")
				.each([](int id, std::string name, std::string tribe, std::string contents, std::string originating_map)
					{
						last_read_message_id = id;

						if (originating_map == mapname.ToString())
						{
							// skip messages that come from the same map.
							return true;
						}

						FString playername(name);
						FString tribename(" ("+tribe+")");
						
						FString map_name(" ["+originating_map+"]");

						ArkApi::GetApiUtils().SendChatMessageToAll(playername + tribename + map_name, contents.c_str());
						Log::GetLog()->info("new chat message: map: " + map_name.ToString() + " player: " + playername.ToString() + " tribe: " + tribename.ToString() + " contents:" + contents);
						return true;
					});
		}
	
	}
	catch (const std::exception& ex)
	{
		Log::GetLog()->warn("problem in PostLatestChat");
		Log::GetLog()->error(ex.what());
		ConnectDatabase();
	}
}

void Hook_AShooterGameMode_InitGame(AShooterGameMode* a_shooter_game_mode, FString* map_name, FString* options,FString* error_message)
{
	AShooterGameMode_InitGame_original(a_shooter_game_mode, map_name, options, error_message);

	Log::GetLog()->warn("Server is ready. attempting to connect to DB...");

	ConnectDatabase();

	Log::GetLog()->info("Begin RecurringExecute");

	API::Timer::Get().RecurringExecute(&PostLatestChat, 1, -1, false);
	
	mapname = *map_name;
}

void Load()
{
	Log::Get().Init("Ark API Cross Chat ");
	Log::GetLog()->info("Ark Cross Chat Plugin has been loaded.");

	ArkApi::GetHooks().SetHook("AShooterPlayerController.ServerSendChatMessage_Implementation", &AShooterPlayerController_ServerSendChatMessage_Impl, &AShooterPlayerController_ServerSendChatMessage_Impl_original);
	ArkApi::GetHooks().SetHook("AShooterGameMode.InitGame", &Hook_AShooterGameMode_InitGame,&AShooterGameMode_InitGame_original);

}
void Unload()
{

}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		Load();
		break;
	case DLL_PROCESS_DETACH:
		Unload();
		break;
	}
	return TRUE;
}



