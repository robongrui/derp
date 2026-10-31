/* The biography in every reader's book: a plain life told day by day
   (biography_template.h has the notation). Written for DERP; nothing in it
   may use a character the library's alphabet lacks. */
#include "biography_template.h"

const char *const Biography_Opening =
    "\v\v\v\v\v\v\v\v\v\v\v\v\v\v\v\v\v\vthe life of $N\f"
    "you were born in $B in $Y, under the sign of $S, and they named you $N.\n"
    "you grew up in $H. <you had $P, and you loved $P more than you ever said|"
    "you never had a pet, though now and then you wanted one>.\n"
    "of all the things there were to eat, you liked $F best.\n"
    "these are your days.\f";

const char *const Biography_Closing =
    "\fand then you died.\n"
    "you woke in the library, in a gallery like every other, under a lamp. "
    "there were books on every shelf, every book was nonsense, and there was no end to the shelves.\n"
    "you walked down. when you were tired you slept, and when you died you woke again in the morning.\n"
    "you took books from the shelves and read them, one after another, and none of them was yours.\n"
    "until one day, on a shelf like every other shelf, you opened this one, "
    "and read your life in it, and it was true.\f";

const char *const Biography_Weekdays[7] = {
    "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday",
};

const char *const Biography_Months[12] = {
    "january", "february", "march", "april", "may", "june",
    "july", "august", "september", "october", "november", "december",
};

const char *const Biography_Days[BIOGRAPHY_DAY_TEMPLATES] = {
    "you {woke early|slept late|woke in the dark|were woken by a noise} and "
    "{made coffee|boiled water for tea|ate standing up|lay still a while}. "
    "{outside|beyond the window} $H was {grey|quiet|wet|bright}, and "
    "{a dog barked somewhere|the street was empty|a bus went past|the bins were out}. "
    "{later|afterwards} you {washed the cups|swept the floor|wrote a list|read the paper}, "
    "{and then the day began|and that was all|and it passed|and nobody called}. "
    "{you remembered $B|you thought of nothing} {until noon|until night}.\n",

    "you walked to the {shop|station|river|park} {the long way|the short way|slowly|quickly}, "
    "past {the school|the church|the laundrette|the empty lot}. "
    "{it rained|it was cold|the sun was out|there was wind}, and you "
    "{wore your good coat|forgot your gloves|carried an umbrella|kept your hands in your pockets}. "
    "{someone waved|nobody looked at you}, "
    "{and you waved back|and you looked away|and you said nothing|and you smiled}. "
    "{on the way home|coming back} you "
    "{bought bread|bought milk|bought nothing|picked up some $F}.\n",

    "you {cleaned the flat|did the washing|fixed a shelf|sorted old letters} while "
    "<$P watched|the flat stayed quiet>. "
    "{around noon|in the afternoon|at one|at three} you "
    "{ate $F|made soup|had bread and cheese|forgot to eat}. "
    "{the phone rang once|a letter came|the neighbours argued|a pipe knocked}, and "
    "{you let it be|you went to see|you wrote it down|you turned the radio up}. "
    "{by evening|by dark|at sunset|at midnight} "
    "{you were tired|you felt better|you felt nothing much|you had a headache}, "
    "{and you slept well|and you slept badly|and you did not sleep|and you dreamed of $B}.\n",

    "you {took the train|took the bus|drove|cycled} to {work|town|the coast|see your mother}. "
    "{the journey|the way} was {long|short|slow|loud}, and you "
    "{read|slept|looked out at the fields|counted the stops}. "
    "{you got there late|you got there early|you arrived on time|you never got there}. "
    "{it was a day like any other|nothing happened that you remembered later|"
    "you met someone you knew from $H|you lost your ticket}. "
    "{on the way back|going home} {it was dark|it was snowing|you were alone|you were happy}.\n",

    "it was {your birthday|a holiday|a sunday|an ordinary day}, and "
    "{you were tired|you were cheerful|you were restless|you were sad}. "
    "{you ate $F|you went for a walk|you called someone|you stayed in bed} "
    "{until noon|until dark|for hours|for a while}. "
    "{people said|you were told} that "
    "{you looked well|you looked tired|you had changed|you had not changed}. "
    "{it was the year|this was the time} "
    "{you moved house|you learned to swim|you broke your arm|you fell in love}. "
    "{you did not believe in stars|you read your stars for $S}, "
    "{and that was that|and you kept it to yourself}.\n",

    "you {sat by the window|sat on the steps|stood at the sink|lay on the floor} and "
    "{watched the rain|listened to the traffic|did nothing at all|waited}. "
    "<$P came and sat with you|nobody came>, and "
    "{the light changed|the clock ticked|the kettle cooled|the room went dark} {slowly|very slowly}. "
    "{you missed $B|you thought about dying|you planned a trip|you counted your money}. "
    "{in the evening|that night} you "
    "{wrote a letter you never sent|called an old friend|cooked $F|went to bed early}. "
    "{it was|it seemed} {a good day|a bad day|a long day|a short day}, "
    "{and you slept|and you did not sleep}.\n",

    "you {went to the doctor|went to the dentist|went to the bank|went to the post office} and "
    "{waited an hour|waited ten minutes|were seen at once|were sent away}. "
    "{the clerk|the woman at the desk} "
    "{knew your name|spelled your name wrong|asked where you were born, and you said $B|was rude}. "
    "{afterwards|on the way out} you "
    "{treated yourself to $F|bought flowers|bought a newspaper|bought nothing} "
    "{and went home|and went to the park|and sat in a cafe|and walked for a long time}. "
    "{the sky was|the clouds were} {low|high|pink|white}, and {it was cold|it was warm}.\n",

    "you {met|ran into|had lunch with|argued with} "
    "{an old friend|your brother|your sister|a neighbour}, "
    "{and you talked about|and you did not talk about} "
    "{the old days|money|the people in $H|the future}. "
    "{it was|it felt} {easy|awkward|warm|strange}. "
    "{you laughed once|you laughed a lot|you did not laugh|you nearly cried}. "
    "{afterwards you|later you} "
    "{felt lighter|felt heavier|forgot about it|thought about it for days}, "
    "{and you never saw them again|and you saw them again soon}.\n",

    "<you fed $P and|you> "
    "{made the bed|opened the curtains|took out the rubbish|watered the plants}. "
    "{the post came|the post did not come}, "
    "{the milk was off|the bread was stale|the heating was broken|the window stuck}, and "
    "{you shrugged|you swore|you laughed|you sighed}. "
    "{you spent the afternoon|you spent the evening} "
    "{reading|sleeping|writing|watching television}. "
    "{dinner was $F|you skipped dinner|you ate out|you had soup}, "
    "{and you went to bed|and you stayed up} {early|late|very late|at the usual time}. "
    "{the night was quiet|the night was loud}.\n",

    "you {were ill|had a cold|had a fever|were well again}, "
    "{and stayed in bed|and went to work anyway}. "
    "<$P lay beside you|the room was quiet>, "
    "{and you listened to the clock|and you listened to the rain|and you dozed|"
    "and you counted the cracks in the ceiling}. "
    "{in your dream you were in $B|you dreamed of falling|you dreamed of water|you did not dream}. "
    "{when you woke|later} "
    "{you felt better|you felt worse|you were hungry|you were thirsty}, and "
    "{someone brought you $F|you made yourself toast|you ate nothing|you drank tea}. "
    "{it took|it lasted} {a day|three days|a week|longer than you thought}, "
    "{and then it passed|and then it was over}.\n",

    "you {cooked $F|baked bread|made a stew|made a cake} {for yourself|for friends}, "
    "and it {came out well|burned|was too salty|was perfect}. "
    "{you ate it|you shared it} {at the table|on the sofa|by the window|standing up}. "
    "{nobody said anything|someone said it was the best they had ever had|"
    "you ate too much|you saved some for later}. "
    "{afterwards you washed up|afterwards you left the dishes}, "
    "{and the kitchen smelled of it|and the flat was warm} "
    "{for days|until morning|all evening|for a long time}. "
    "{you were content|you were lonely|you were happy|you were tired}.\n",

    "you {found|lost|threw away|gave away} {a photograph|an old key|a ring|a book} "
    "{that came from $B|from years ago}. "
    "{you held it|you looked at it} {for a long time|for a moment|and smiled|and put it away}. "
    "{it made you think of|it reminded you of} {someone|a house|a summer|a mistake} "
    "{you had forgotten|you had nearly forgotten}. "
    "{you never told anyone|you told a friend}. "
    "{that night|that week} {it rained|it snowed|the wind blew|nothing happened}.\n",

    "{the year was|that winter was|that summer was|the spring was} {hard|easy|long|short}. "
    "you {worked|studied|looked for work|stayed home} {most days|every day}, and "
    "{saved a little|spent too much|got by|owed money}. "
    "{at home in $H|in town} "
    "{the shops changed|the trees were cut down|a new road was built|nothing changed}. "
    "{you noticed|you did not notice} that <$P was getting old|the flat was too quiet>. "
    "{you were|you felt} {older|younger|the same|different}.\n",

    "you {went to the sea|went to the mountains|went back to $B|went nowhere}. "
    "{it took|the trip took} {all day|two days|an hour|forever}. "
    "{there you|once there you} {walked|swam|slept|ate $F} {a lot|a little}, and "
    "{took no pictures|took one picture|took many pictures|lost your camera}. "
    "{the weather was|the people were} {kind|cold|strange|wonderful}. "
    "{you wanted to stay|you wanted to go home|you could not decide|you did not care}.\n",

    "{in the night|before dawn} you {woke|lay awake} and "
    "{listened|thought|worried|counted}. "
    "{the street outside was|the house was} {silent|loud}, and "
    "{the moon was out|it was raining}. "
    "{your heart was|your mind was} {loud|quiet|racing|slow}. "
    "{in the end you|at last you} {slept|got up|turned on the light|gave up}. "
    "{the morning came|the alarm went off} {too soon|at last}, "
    "{and you were tired|and you were fine}.\n",

    "you {read a book about $S|bought a calendar|wrote in a diary|made a promise} "
    "{and kept it|and forgot it}. "
    "you were born under $S, {they said|you said}, and "
    "{that explained everything|that explained nothing|you did not believe it|you half believed it}. "
    "{you went out to|you stayed in to} {see friends|watch a film|play cards|be alone}. "
    "{it was late when|it was early when} you "
    "{got home|went to sleep|said goodnight|turned off the lights}, "
    "{and outside $H|and the town} was {asleep|awake|dark|lit up}. "
    "{it was a good day|it was not a bad day}.\n",
};
