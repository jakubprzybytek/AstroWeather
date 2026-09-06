export type AstroResponse = {
  configId: string;
  timezone: string;
  sun: {
    rise: string | null;
    set: string | null;
  };
  moon: {
    rise: string | null;
    set: string | null;
    alwaysUp: boolean;
    alwaysDown: boolean;
  };
};
