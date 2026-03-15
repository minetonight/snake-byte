graphics
1) events lenght;
2) event type        - 0-4|
3) event anim start - "0|"
4) event anim end   - "500|"
5) events params; 
    - bird.id       - "136x "
    - others        "x x x x x|"


    public static final int MOVE = 0;
    public static final int FALL = 1;
    public static final int EAT = 2;
    public static final int DEATH = 3;
    public static final int BEHEAD = 4;

e.type = EventData.DEATH; // 3
        e.params = new int[2];
        e.params[0] = bird.id;
        e.params[1] = bird.owner.getIndex();
    
e.type = EventData.BEHEAD; // 4
        e.params = new int[2];
        int idx = 0;
        e.params[idx++] = bird.id;
        e.params[idx++] = bird.owner.getIndex();

e.type = EventData.FALL; // 1
        e.params = new int[3];
        int idx = 0;
        e.params[idx++] = bird.id;
        e.params[idx++] = bird.owner.getIndex();
        e.params[idx++] = numberOfCells;

e.type = EventData.MOVE; // 0
        e.params = new int[5];
        int idx = 0;
        e.params[idx++] = bird.id;
        e.params[idx++] = bird.owner.getIndex();
        e.params[idx++] = bird.getHeadPos().getX();
        e.params[idx++] = bird.getHeadPos().getY();
        e.params[idx++] = willEatApple ? 1 : 0;


e.type = EventData.EAT; // 2
        e.params = new int[5];
        int idx = 0;
        e.params[idx++] = bird.id;
        e.params[idx++] = bird.owner.getIndex();
        e.params[idx++] = growth;
        e.params[idx++] = coord.getX();
        e.params[idx++] = coord.getY();
